// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVersionNumber>

#include <utility>

namespace update {

/// Target packaging format of the running installation, used to pick the matching GitHub
/// release asset for a one-click update path.
enum class Platform {
    Windows,
    MacOS,
    LinuxFlatpak,
    LinuxDeb,
    LinuxRpm,
    LinuxArch,
    LinuxTarball,
    Android,
    Unknown,
};

struct ReleaseInfo {
    bool valid = false;
    bool updateAvailable = false;
    QString tagName;
    QString version;       ///< normalised version without leading 'v'
    QString releaseNotes;
    QString publishedDate; ///< YYYY-MM-DD
    QUrl releaseUrl;
    QString assetName;
    QUrl assetUrl;
    qint64 assetSize = 0;
    QByteArray assetSha256;  ///< lower-case hex from the asset's "digest" field; empty if absent
};

/// Splits a version tag like "v0.2.2-rc1" into its numeric part and optional pre-release suffix.
inline std::pair<QVersionNumber, QString> parseVersion(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) s.remove(0, 1);
    const qsizetype plus = s.indexOf(QLatin1Char('+'));
    if (plus >= 0) s.truncate(plus);  // ignore build metadata (SemVer 2.0)
    QString prerelease;
    const qsizetype dash = s.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        prerelease = s.mid(dash + 1);
        s.truncate(dash);
    }
    qsizetype suffixIdx = 0;
    const QVersionNumber num = QVersionNumber::fromString(s, &suffixIdx);
    if (num.isNull() || suffixIdx != s.size()) return {{}, {}};
    return {num, prerelease};
}

/// True if `candidate` represents a strictly newer version than `current`.
/// A pre-release (e.g. 0.3.0-rc1) is older than its corresponding release (0.3.0).
inline bool isNewerVersion(const QString& candidate, const QString& current)
{
    const auto [candNum, candPre] = parseVersion(candidate);
    const auto [currNum, currPre] = parseVersion(current);
    if (candNum.isNull() || currNum.isNull()) return false;
    const int cmp = QVersionNumber::compare(candNum, currNum);
    if (cmp != 0) return cmp > 0;
    // Same numeric version: final release > pre-release.
    if (currPre.isEmpty() && !candPre.isEmpty()) return false;
    if (!currPre.isEmpty() && candPre.isEmpty()) return true;
    // SemVer 2.0 §11.4: compare dot-separated pre-release identifiers (numeric < alphanumeric).
    const QStringList candParts = candPre.split(QLatin1Char('.'));
    const QStringList currParts = currPre.split(QLatin1Char('.'));
    const qsizetype n = std::min(candParts.size(), currParts.size());
    for (qsizetype i = 0; i < n; ++i) {
        if (candParts[i] == currParts[i]) continue;
        bool candIsNum = false, currIsNum = false;
        const qulonglong candVal = candParts[i].toULongLong(&candIsNum);
        const qulonglong currVal = currParts[i].toULongLong(&currIsNum);
        if (candIsNum && currIsNum) return candVal > currVal;
        if (candIsNum != currIsNum) return !candIsNum;  // numeric has lower precedence
        return candParts[i] > currParts[i];
    }
    return candParts.size() > currParts.size();
}

/// Preferred file suffixes (most specific first) for a given platform.
inline QStringList preferredAssetSuffixes(Platform platform)
{
    switch (platform) {
    case Platform::Windows: return {QStringLiteral(".exe")};
    case Platform::MacOS: return {QStringLiteral(".dmg")};
    case Platform::LinuxFlatpak: return {QStringLiteral(".flatpak"), QStringLiteral(".tar.gz")};
    case Platform::LinuxDeb: return {QStringLiteral(".deb"), QStringLiteral(".tar.gz")};
    case Platform::LinuxRpm: return {QStringLiteral(".rpm"), QStringLiteral(".tar.gz")};
    case Platform::LinuxArch: return {QStringLiteral(".pkg.tar.zst"), QStringLiteral(".tar.gz")};
    case Platform::LinuxTarball: return {QStringLiteral(".tar.gz")};
    case Platform::Android: return {QStringLiteral(".apk")};
    case Platform::Unknown: return {};
    }
    return {};
}

/// Parses the JSON payload of GitHub's `/repos/<owner>/<repo>/releases/latest` endpoint.
inline ReleaseInfo parseGitHubRelease(const QByteArray& json, const QString& currentVersion, Platform platform)
{
    ReleaseInfo info;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return info;

    const QJsonObject obj = doc.object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
    const auto [ver, pre] = parseVersion(tag);
    if (ver.isNull()) return info;

    info.valid = true;
    info.tagName = tag;
    info.version = pre.isEmpty() ? ver.toString() : QStringLiteral("%1-%2").arg(ver.toString(), pre);
    info.updateAvailable = isNewerVersion(tag, currentVersion);
    info.releaseUrl = QUrl(obj.value(QStringLiteral("html_url")).toString().trimmed());
    info.releaseNotes = obj.value(QStringLiteral("body")).toString().trimmed();
    const QString published = obj.value(QStringLiteral("published_at")).toString().trimmed();
    if (published.size() >= 10) info.publishedDate = published.left(10);

    // Pick the best matching release asset for the running platform.
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const QString& suffix : preferredAssetSuffixes(platform)) {
        for (const QJsonValue& val : assets) {
            const QJsonObject a = val.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            const QUrl url(a.value(QStringLiteral("browser_download_url")).toString());
            if (name.endsWith(suffix, Qt::CaseInsensitive) && url.isValid()) {
                info.assetName = name;
                info.assetUrl = url;
                info.assetSize = a.value(QStringLiteral("size")).toInteger(0);
                // GitHub publishes "digest": "sha256:<hex>" for release assets.
                const QString digest = a.value(QStringLiteral("digest")).toString().trimmed();
                if (digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive)) {
                    const QByteArray hex = digest.mid(7).toLatin1().toLower();
                    if (hex.size() == 64 && QByteArray::fromHex(hex).size() == 32) info.assetSha256 = hex;
                }
                return info;
            }
        }
    }
    return info;
}

}  // namespace update
