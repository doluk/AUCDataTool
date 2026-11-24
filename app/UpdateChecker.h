// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "UpdateInfo.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

/// Checks GitHub Releases for newer versions of AUCDataTool and provides a platform-aware
/// download/install path.
class UpdateChecker : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QUrl releasesUrl READ releasesUrl CONSTANT)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool checked READ checked NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool bannerVisible READ bannerVisible NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY stateChanged)
    Q_PROPERTY(QString publishedDate READ publishedDate NOTIFY stateChanged)
    Q_PROPERTY(QString assetName READ assetName NOTIFY stateChanged)
    Q_PROPERTY(QUrl assetUrl READ assetUrl NOTIFY stateChanged)
    Q_PROPERTY(qint64 assetSize READ assetSize NOTIFY stateChanged)
    Q_PROPERTY(QString installActionLabel READ installActionLabel NOTIFY stateChanged)
    Q_PROPERTY(QString installHint READ installHint NOTIFY stateChanged)
    Q_PROPERTY(QString installCommand READ installCommand NOTIFY downloadChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(bool error READ hasError NOTIFY stateChanged)

    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
    Q_PROPERTY(qint64 downloadBytes READ downloadBytes NOTIFY downloadChanged)
    Q_PROPERTY(qint64 downloadTotalBytes READ downloadTotalBytes NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadedFile READ downloadedFile NOTIFY downloadChanged)

    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY settingsChanged)
    Q_PROPERTY(QString skippedVersion READ skippedVersion NOTIFY settingsChanged)

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    static QString currentVersion();
    static QUrl releasesUrl();

    bool checking() const { return m_checkReply != nullptr; }
    bool checked() const { return m_checked; }
    bool updateAvailable() const { return m_info.valid && m_info.updateAvailable; }
    bool bannerVisible() const;
    QString latestVersion() const { return m_info.version; }
    QUrl releaseUrl() const { return m_info.releaseUrl.isValid() ? m_info.releaseUrl : releasesUrl(); }
    QString releaseNotes() const { return m_info.releaseNotes; }
    QString publishedDate() const { return m_info.publishedDate; }
    QString assetName() const { return m_info.assetName; }
    QUrl assetUrl() const { return m_info.assetUrl; }
    qint64 assetSize() const { return m_info.assetSize; }
    QString installActionLabel() const;
    QString installHint() const;
    QString installCommand() const { return m_installCommand; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasError() const { return m_hasError; }

    bool downloading() const { return m_downloadReply != nullptr; }
    double downloadProgress() const { return m_downloadProgress; }
    qint64 downloadBytes() const { return m_downloadBytes; }
    qint64 downloadTotalBytes() const { return m_downloadTotalBytes; }
    QString downloadedFile() const { return m_downloadedFile; }

    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    QString skippedVersion() const { return m_skippedVersion; }

    /// Queries GitHub Releases. Automatic checks (`manual == false`) respect `autoCheck`,
    /// throttle to once every 6 hours, and stay silent on network errors.
    Q_INVOKABLE void checkForUpdates(bool manual = false);
    /// Downloads the matching platform installer/package (or opens the download/release URL).
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void openDownloadedFolder();
    Q_INVOKABLE void copyInstallCommand() const;
    Q_INVOKABLE void dismissBanner();
    Q_INVOKABLE void skipLatestVersion();

signals:
    void stateChanged();
    void downloadChanged();
    void settingsChanged();
    /// Emitted after launching a Windows installer so the app closes and releases locked binaries.
    void readyToQuitForUpdate();

private:
    static update::Platform detectPlatform();
    void finishCheck(QNetworkReply* reply, bool manual);
    void finishDownload(QNetworkReply* reply);
    void cleanupDownloadFile(bool removeFile);
    /// Appends to the download file and its hash; false (and m_downloadError set) on a write error.
    bool writeDownloadChunk(const QByteArray& data);
    /// Flatpak installs update through flatpak/the software centre, never from a download.
    bool notifyOnly() const { return m_platform == update::Platform::LinuxFlatpak; }
    void applyReleaseInfo(const update::ReleaseInfo& info);
    QString buildInstallCommand(const QString& path) const;

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_checkReply;
    QPointer<QNetworkReply> m_downloadReply;
    std::unique_ptr<QFile> m_downloadFile;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    QString m_downloadError;  ///< write error that aborted the download
    QString m_downloadPath;

    update::Platform m_platform = update::Platform::Unknown;
    update::ReleaseInfo m_info;
    bool m_checked = false;
    bool m_dismissed = false;
    bool m_hasError = false;
    QString m_statusMessage;

    double m_downloadProgress = 0.0;
    qint64 m_downloadBytes = 0;
    qint64 m_downloadTotalBytes = 0;
    QString m_downloadedFile;
    QString m_installCommand;

    bool m_autoCheck = true;
    QString m_skippedVersion;
};
