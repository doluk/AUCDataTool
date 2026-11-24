// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "UpdateInfo.h"

#include <QTest>

using namespace update;

class TestUpdateInfo : public QObject {
    Q_OBJECT
private slots:
    void versionParsing()
    {
        {
            const auto [v, pre] = parseVersion(QStringLiteral("v0.2.1"));
            QCOMPARE(v, QVersionNumber(0, 2, 1));
            QVERIFY(pre.isEmpty());
        }
        {
            const auto [v, pre] = parseVersion(QStringLiteral("V1.3.0-rc.2+build42"));
            QCOMPARE(v, QVersionNumber(1, 3, 0));
            QCOMPARE(pre, QStringLiteral("rc.2"));
        }
        {
            const auto [v, pre] = parseVersion(QStringLiteral("not-a-version"));
            QVERIFY(v.isNull());
            QVERIFY(pre.isEmpty());
        }
    }

    void versionComparison()
    {
        QVERIFY(isNewerVersion(QStringLiteral("v0.2.2"), QStringLiteral("0.2.1")));
        QVERIFY(isNewerVersion(QStringLiteral("0.3.0"), QStringLiteral("0.2.9")));
        QVERIFY(isNewerVersion(QStringLiteral("1.0"), QStringLiteral("0.9.9")));
        QVERIFY(!isNewerVersion(QStringLiteral("v0.2.1"), QStringLiteral("0.2.1")));
        QVERIFY(!isNewerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.1")));

        // Pre-release ordering (SemVer 2.0)
        QVERIFY(!isNewerVersion(QStringLiteral("0.3.0-rc.1"), QStringLiteral("0.3.0")));
        QVERIFY(isNewerVersion(QStringLiteral("0.3.0"), QStringLiteral("0.3.0-rc.1")));
        QVERIFY(isNewerVersion(QStringLiteral("0.3.0-rc.2"), QStringLiteral("0.3.0-rc.1")));
        QVERIFY(isNewerVersion(QStringLiteral("0.3.0-rc.10"), QStringLiteral("0.3.0-rc.2")));
        QVERIFY(isNewerVersion(QStringLiteral("0.3.0-rc.1"), QStringLiteral("0.3.0-beta.5")));
        QVERIFY(!isNewerVersion(QStringLiteral("invalid"), QStringLiteral("0.2.1")));
    }

    void parseReleaseAssetsPerPlatform()
    {
        const QByteArray payload = QByteArrayLiteral(
            "{"
            "\"tag_name\": \"v0.3.0\","
            "\"html_url\": \"https://github.com/doluk/AUCDataTool/releases/tag/v0.3.0\","
            "\"published_at\": \"2026-12-01T10:20:30Z\","
            "\"body\": \"### Changes\\n- Update mechanism\\n- File associations\","
            "\"assets\": ["
            "{\"name\": \"AUCDataTool-0.3.0-win64.exe\", \"browser_download_url\": \"https://example.com/win.exe\", \"size\": 25000000,"
            " \"digest\": \"sha256:9F86D081884C7D659A2FEAA0C55AD015A3BF4F1B2B0B822CD15D6C15B0F00A08\"},"
            "{\"name\": \"AUCDataTool-0.3.0-Darwin.dmg\", \"browser_download_url\": \"https://example.com/mac.dmg\", \"size\": 30000000},"
            "{\"name\": \"aucdatatool_0.3.0_amd64.deb\", \"browser_download_url\": \"https://example.com/app.deb\", \"size\": 20000000},"
            "{\"name\": \"AUCDataTool-0.3.0-Linux.rpm\", \"browser_download_url\": \"https://example.com/app.rpm\", \"size\": 21000000},"
            "{\"name\": \"AUCDataTool-0.3.0-Linux.tar.gz\", \"browser_download_url\": \"https://example.com/app.tar.gz\", \"size\": 22000000},"
            "{\"name\": \"AUCDataTool-x86_64.flatpak\", \"browser_download_url\": \"https://example.com/app.flatpak\", \"size\": 15000000},"
            "{\"name\": \"aucdatatool-0.3.0-1-x86_64.pkg.tar.zst\", \"browser_download_url\": \"https://example.com/app.pkg.tar.zst\", \"size\": 8000000},"
            "{\"name\": \"AUCDataTool-arm64-v8a.apk\", \"browser_download_url\": \"https://example.com/app.apk\", \"size\": 18000000}"
            "]"
            "}");

        const ReleaseInfo win = parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::Windows);
        QVERIFY(win.valid);
        QVERIFY(win.updateAvailable);
        QCOMPARE(win.version, QStringLiteral("0.3.0"));
        QCOMPARE(win.publishedDate, QStringLiteral("2026-12-01"));
        QCOMPARE(win.assetName, QStringLiteral("AUCDataTool-0.3.0-win64.exe"));
        QCOMPARE(win.assetUrl, QUrl(QStringLiteral("https://example.com/win.exe")));
        QCOMPARE(win.assetSize, 25000000);
        QCOMPARE(win.assetSha256, QByteArray("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
        QVERIFY(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::MacOS).assetSha256.isEmpty());

        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::MacOS).assetName,
                 QStringLiteral("AUCDataTool-0.3.0-Darwin.dmg"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::LinuxDeb).assetName,
                 QStringLiteral("aucdatatool_0.3.0_amd64.deb"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::LinuxRpm).assetName,
                 QStringLiteral("AUCDataTool-0.3.0-Linux.rpm"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::LinuxFlatpak).assetName,
                 QStringLiteral("AUCDataTool-x86_64.flatpak"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::LinuxArch).assetName,
                 QStringLiteral("aucdatatool-0.3.0-1-x86_64.pkg.tar.zst"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::LinuxTarball).assetName,
                 QStringLiteral("AUCDataTool-0.3.0-Linux.tar.gz"));
        QCOMPARE(parseGitHubRelease(payload, QStringLiteral("0.2.1"), Platform::Android).assetName,
                 QStringLiteral("AUCDataTool-arm64-v8a.apk"));

        // Same version -> not an update
        const ReleaseInfo same = parseGitHubRelease(payload, QStringLiteral("0.3.0"), Platform::Windows);
        QVERIFY(same.valid);
        QVERIFY(!same.updateAvailable);
    }

    void fallbackAndMalformedJson()
    {
        const QByteArray tarballOnly = QByteArrayLiteral(
            "{\"tag_name\": \"v0.4.0\","
            "\"assets\": [{\"name\": \"AUCDataTool-0.4.0-Linux.tar.gz\", \"browser_download_url\": \"https://example.com/a.tar.gz\", \"size\": 100}]}");
        const ReleaseInfo debFallback = parseGitHubRelease(tarballOnly, QStringLiteral("0.2.1"), Platform::LinuxDeb);
        QVERIFY(debFallback.valid);
        QCOMPARE(debFallback.assetName, QStringLiteral("AUCDataTool-0.4.0-Linux.tar.gz"));

        QVERIFY(!parseGitHubRelease(QByteArrayLiteral("{broken"), QStringLiteral("0.2.1"), Platform::Windows).valid);
        QVERIFY(!parseGitHubRelease(QByteArrayLiteral("{\"tag_name\": \"nope\"}"), QStringLiteral("0.2.1"), Platform::Windows).valid);
    }
};

QTEST_GUILESS_MAIN(TestUpdateInfo)
#include "tst_updateinfo.moc"
