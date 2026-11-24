// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "UpdateChecker.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr char kLatestReleaseApi[] = "https://api.github.com/repos/doluk/AUCDataTool/releases/latest";
constexpr char kReleasesPage[] = "https://github.com/doluk/AUCDataTool/releases";
constexpr qint64 kAutoCheckIntervalSecs = 6 * 3600;  // avoid hitting GitHub's unauthenticated rate limit

QNetworkRequest makeRequest(const QUrl& url, bool jsonApi)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(jsonApi ? 15000 : 60000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AUCDataTool/%1").arg(QStringLiteral(PROJECT_VERSION)));
    if (jsonApi) {
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    return req;
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_platform(detectPlatform())
{
    QSettings s;
    s.beginGroup(QStringLiteral("updates"));
    m_autoCheck = s.value(QStringLiteral("autoCheck"), true).toBool();
    m_skippedVersion = s.value(QStringLiteral("skippedVersion")).toString();
    // The automatic check runs at most every 6 h; the last answer keeps the banner up in between.
    const QByteArray cached = s.value(QStringLiteral("releaseJson")).toByteArray();
    s.endGroup();
    if (!cached.isEmpty()) {
        const update::ReleaseInfo info = update::parseGitHubRelease(cached, currentVersion(), m_platform);
        if (info.valid) applyReleaseInfo(info);
    }
}

void UpdateChecker::applyReleaseInfo(const update::ReleaseInfo& info)
{
    m_info = info;
    m_statusMessage = m_info.updateAvailable
                          ? tr("Version %1 is available (you have %2).").arg(m_info.version, currentVersion())
                          : tr("AUCDataTool %1 is up to date.").arg(currentVersion());
}

UpdateChecker::~UpdateChecker()
{
    cancelDownload();
    if (m_checkReply) {
        m_checkReply->disconnect(this);
        m_checkReply->abort();
        m_checkReply->deleteLater();
    }
}

QString UpdateChecker::currentVersion()
{
    return QStringLiteral(PROJECT_VERSION);
}

QUrl UpdateChecker::releasesUrl()
{
    return QUrl(QString::fromLatin1(kReleasesPage));
}

update::Platform UpdateChecker::detectPlatform()
{
#if defined(Q_OS_WIN)
    return update::Platform::Windows;
#elif defined(Q_OS_MACOS)
    return update::Platform::MacOS;
#elif defined(Q_OS_ANDROID)
    return update::Platform::Android;
#elif defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsSet("FLATPAK_ID") || QFileInfo::exists(QStringLiteral("/.flatpak-info")))
        return update::Platform::LinuxFlatpak;

    // Inspect /etc/os-release to match the distribution's package format.
    QFile f(QStringLiteral("/etc/os-release"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(f.readAll()).toLower();
        QString id, idLike;
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("id="))) id = line.mid(3).remove(QLatin1Char('"')).trimmed();
            else if (line.startsWith(QLatin1String("id_like="))) idLike = line.mid(8).remove(QLatin1Char('"')).trimmed();
        }
        const QString combined = id + QLatin1Char(' ') + idLike;
        if (combined.contains(QLatin1String("arch")) || combined.contains(QLatin1String("manjaro")))
            return update::Platform::LinuxArch;
        if (combined.contains(QLatin1String("debian")) || combined.contains(QLatin1String("ubuntu")))
            return update::Platform::LinuxDeb;
        if (combined.contains(QLatin1String("fedora")) || combined.contains(QLatin1String("rhel"))
            || combined.contains(QLatin1String("centos")) || combined.contains(QLatin1String("almalinux"))
            || combined.contains(QLatin1String("rocky")) || combined.contains(QLatin1String("suse")))
            return update::Platform::LinuxRpm;
    }
    return update::Platform::LinuxTarball;
#else
    return update::Platform::Unknown;
#endif
}

bool UpdateChecker::bannerVisible() const
{
    if (!updateAvailable() || m_dismissed) return false;
    return m_skippedVersion.isEmpty() || m_skippedVersion != m_info.version;
}

QString UpdateChecker::installActionLabel() const
{
    if (!m_info.assetUrl.isValid() || notifyOnly()) return tr("Open release page");
    switch (m_platform) {
    case update::Platform::Windows: return tr("Download & install");
    case update::Platform::MacOS: return tr("Download & open (.dmg)");
    case update::Platform::Android: return tr("Download APK");
    case update::Platform::LinuxFlatpak:
    case update::Platform::LinuxDeb:
    case update::Platform::LinuxRpm:
    case update::Platform::LinuxArch:
    case update::Platform::LinuxTarball:
        return tr("Download %1").arg(m_info.assetName);
    case update::Platform::Unknown:
        return tr("Open release page");
    }
    return tr("Open release page");
}

QString UpdateChecker::installHint() const
{
    if (notifyOnly())
        return tr("This is a Flatpak installation: update it in your software centre or with "
                  "\"flatpak update\". If you installed a .flatpak bundle, download the new bundle "
                  "from the release page.");
    if (!m_info.assetUrl.isValid()) return tr("Download the matching installer from the GitHub release page.");
    switch (m_platform) {
    case update::Platform::Windows:
        return tr("Downloads the Windows installer (%1), starts it, and closes AUCDataTool.").arg(m_info.assetName);
    case update::Platform::MacOS:
        return tr("Downloads %1 and opens it so you can drag AUCDataTool to Applications.").arg(m_info.assetName);
    case update::Platform::Android:
        return tr("Opens the APK download in your browser so Android can install the update.");
    case update::Platform::LinuxFlatpak:
    case update::Platform::LinuxDeb:
    case update::Platform::LinuxRpm:
    case update::Platform::LinuxArch:
    case update::Platform::LinuxTarball:
        return tr("Downloads %1 to your Downloads folder and shows the install command.").arg(m_info.assetName);
    case update::Platform::Unknown:
        return {};
    }
    return {};
}

void UpdateChecker::setAutoCheck(bool on)
{
    if (on == m_autoCheck) return;
    m_autoCheck = on;
    QSettings s;
    s.setValue(QStringLiteral("updates/autoCheck"), on);
    emit settingsChanged();
}

void UpdateChecker::checkForUpdates(bool manual)
{
    if (m_checkReply) return;

    if (!manual) {
        if (!m_autoCheck) return;
        QSettings s;
        const qint64 last = s.value(QStringLiteral("updates/lastCheck"), 0).toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (last > 0 && now - last < kAutoCheckIntervalSecs) return;
    }

    if (!QSslSocket::supportsSsl()) {
        if (manual) {
            m_checked = true;
            m_hasError = true;
            m_statusMessage = tr("HTTPS is not available on this system. Open the release page to check manually.");
            emit stateChanged();
        }
        return;
    }

    if (manual) {
        m_hasError = false;
        m_statusMessage = tr("Checking for updates…");
    }

    QNetworkReply* reply = m_nam.get(makeRequest(QUrl(QString::fromLatin1(kLatestReleaseApi)), true));
    m_checkReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] { finishCheck(reply, manual); });
    emit stateChanged();
}

void UpdateChecker::finishCheck(QNetworkReply* reply, bool manual)
{
    if (m_checkReply == reply) m_checkReply = nullptr;
    reply->deleteLater();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        if (manual) {
            m_checked = true;
            m_hasError = true;
            if (httpStatus == 404)
                m_statusMessage = tr("No published releases were found yet.");
            else if (httpStatus == 429
                     || (httpStatus == 403 && reply->rawHeader("x-ratelimit-remaining") == "0"))
                m_statusMessage = tr("GitHub API rate limit reached. Try again later or open the release page.");
            else
                m_statusMessage = tr("Update check failed: %1").arg(reply->errorString());
        }
        emit stateChanged();
        return;
    }

    const QByteArray json = reply->readAll();
    const update::ReleaseInfo parsed = update::parseGitHubRelease(json, currentVersion(), m_platform);
    if (!parsed.valid) {
        if (manual) {
            m_checked = true;
            m_hasError = true;
            m_statusMessage = tr("Could not read release information from GitHub.");
        }
        emit stateChanged();
        return;
    }

    applyReleaseInfo(parsed);
    m_checked = true;
    m_hasError = false;
    QSettings s;
    s.setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentSecsSinceEpoch());
    s.setValue(QStringLiteral("updates/releaseJson"), json);
    emit stateChanged();
}

void UpdateChecker::downloadAndInstall()
{
    if (m_downloadReply) return;

    if (!m_info.assetUrl.isValid() || notifyOnly()) {
        openReleasePage();
        return;
    }

    // Android: delegating the APK URL to the browser uses the system download manager and
    // package installer without requiring REQUEST_INSTALL_PACKAGES or a FileProvider.
    if (m_platform == update::Platform::Android) {
        QDesktopServices::openUrl(m_info.assetUrl);
        return;
    }

    const QString targetDir = m_platform == update::Platform::Windows
        ? QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString fallbackDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(targetDir.isEmpty() ? fallbackDir : targetDir);
    dir.mkpath(QStringLiteral("."));

    const QString fileName = QFileInfo(m_info.assetName).fileName();
    if (fileName.isEmpty()) {
        openReleasePage();
        return;
    }
    const QString finalPath = dir.filePath(fileName);
    m_downloadPath = finalPath + QStringLiteral(".part");

    m_downloadFile = std::make_unique<QFile>(m_downloadPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_hasError = true;
        m_statusMessage = tr("Cannot write %1: %2").arg(m_downloadPath, m_downloadFile->errorString());
        m_downloadFile.reset();
        emit stateChanged();
        return;
    }

    m_downloadHash.reset();
    m_downloadError.clear();
    m_downloadedFile.clear();
    m_installCommand.clear();
    m_downloadBytes = 0;
    m_downloadTotalBytes = m_info.assetSize;
    m_downloadProgress = 0.0;
    m_hasError = false;
    m_statusMessage = tr("Downloading %1…").arg(fileName);

    QNetworkReply* reply = m_nam.get(makeRequest(m_info.assetUrl, false));
    m_downloadReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (!writeDownloadChunk(reply->readAll())) reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_downloadBytes = received;
        if (total > 0) m_downloadTotalBytes = total;
        m_downloadProgress = m_downloadTotalBytes > 0 ? double(received) / double(m_downloadTotalBytes) : 0.0;
        emit downloadChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishDownload(reply); });
    emit stateChanged();
    emit downloadChanged();
}

void UpdateChecker::finishDownload(QNetworkReply* reply)
{
    if (m_downloadReply == reply) m_downloadReply = nullptr;
    auto err = reply->error();
    const QString errStr = reply->errorString();
    if (err == QNetworkReply::NoError && !writeDownloadChunk(reply->readAll())) err = QNetworkReply::UnknownContentError;
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        cleanupDownloadFile(true);
        if (!m_downloadError.isEmpty()) {
            m_hasError = true;
            m_statusMessage = tr("Could not save the download: %1").arg(m_downloadError);
        } else if (err != QNetworkReply::OperationCanceledError) {
            m_hasError = true;
            m_statusMessage = tr("Download failed: %1").arg(errStr);
        } else {
            m_statusMessage = tr("Download cancelled.");
        }
        emit stateChanged();
        emit downloadChanged();
        return;
    }

    // Refuse a file that does not match the digest GitHub published for the asset.
    if (!m_info.assetSha256.isEmpty() && m_downloadHash.result().toHex() != m_info.assetSha256) {
        cleanupDownloadFile(true);
        m_hasError = true;
        m_statusMessage = tr("The downloaded file is damaged or was modified (SHA-256 mismatch) and was deleted. "
                             "Try again or download it from the release page.");
        emit stateChanged();
        emit downloadChanged();
        return;
    }

    const QString partPath = m_downloadPath;
    const QString finalPath = partPath.endsWith(QLatin1String(".part")) ? partPath.chopped(5) : partPath;
    cleanupDownloadFile(false);
    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        QFile::remove(partPath);
        m_hasError = true;
        m_statusMessage = tr("Could not save downloaded file to %1.").arg(finalPath);
        emit stateChanged();
        emit downloadChanged();
        return;
    }

    m_downloadedFile = finalPath;
    m_downloadProgress = 1.0;
    m_installCommand = buildInstallCommand(finalPath);

    if (m_platform == update::Platform::Windows) {
        // QDesktopServices::openUrl uses ShellExecuteW on Windows, which properly prompts for
        // UAC elevation when launching the NSIS installer (unlike QProcess::startDetached).
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(finalPath))) {
            m_statusMessage = tr("Installer started. Closing AUCDataTool…");
            emit stateChanged();
            emit downloadChanged();
            QTimer::singleShot(400, this, &UpdateChecker::readyToQuitForUpdate);
            return;
        }
        m_hasError = true;
        m_statusMessage = tr("Downloaded to %1, but could not launch the installer automatically.").arg(finalPath);
    } else if (m_platform == update::Platform::MacOS) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(finalPath));
        m_statusMessage = tr("Opened %1. Drag AUCDataTool to Applications to finish updating.").arg(QFileInfo(finalPath).fileName());
    } else {
        m_statusMessage = tr("Downloaded to %1.").arg(finalPath);
    }
    emit stateChanged();
    emit downloadChanged();
}

void UpdateChecker::cancelDownload()
{
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply = nullptr;
    }
    cleanupDownloadFile(true);
    m_downloadProgress = 0.0;
    emit downloadChanged();
}

bool UpdateChecker::writeDownloadChunk(const QByteArray& data)
{
    if (!m_downloadFile) return m_downloadError.isEmpty();
    if (data.isEmpty()) return true;
    if (m_downloadFile->write(data) != data.size()) {
        m_downloadError = m_downloadFile->errorString();
        return false;
    }
    m_downloadHash.addData(data);
    return true;
}

void UpdateChecker::cleanupDownloadFile(bool removeFile)
{
    if (m_downloadFile) {
        m_downloadFile->flush();
        m_downloadFile->close();
        m_downloadFile.reset();
    }
    if (removeFile && !m_downloadPath.isEmpty()) QFile::remove(m_downloadPath);
    m_downloadPath.clear();
}

QString UpdateChecker::buildInstallCommand(const QString& path) const
{
    const QString quoted = QStringLiteral("'%1'").arg(QString(path).replace(QLatin1Char('\''), QLatin1String("'\\''")));
    if (path.endsWith(QLatin1String(".flatpak"), Qt::CaseInsensitive))
        return QStringLiteral("flatpak install --user %1").arg(quoted);
    if (path.endsWith(QLatin1String(".deb"), Qt::CaseInsensitive))
        return QStringLiteral("sudo apt install %1").arg(quoted);
    if (path.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
        return QStringLiteral("sudo dnf install %1").arg(quoted);
    if (path.endsWith(QLatin1String(".pkg.tar.zst"), Qt::CaseInsensitive))
        return QStringLiteral("sudo pacman -U %1").arg(quoted);
    if (path.endsWith(QLatin1String(".tar.gz"), Qt::CaseInsensitive))
        return QStringLiteral("tar -xzf %1").arg(quoted);
    return {};
}

void UpdateChecker::openReleasePage()
{
    QDesktopServices::openUrl(releaseUrl());
}

void UpdateChecker::openDownloadedFolder()
{
    if (m_downloadedFile.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_downloadedFile).absolutePath()));
}

void UpdateChecker::copyInstallCommand() const
{
    if (m_installCommand.isEmpty()) return;
    if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(m_installCommand);
}

void UpdateChecker::dismissBanner()
{
    if (m_dismissed) return;
    m_dismissed = true;
    emit stateChanged();
}

void UpdateChecker::skipLatestVersion()
{
    if (m_info.version.isEmpty()) return;
    m_skippedVersion = m_info.version;
    QSettings s;
    s.setValue(QStringLiteral("updates/skippedVersion"), m_skippedVersion);
    emit settingsChanged();
    emit stateChanged();
}
