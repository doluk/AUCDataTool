// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "FileOpenRouter.h"

#include "AppController.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QWindow>

#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/private/qjnihelpers_p.h>
#endif

namespace {

/// Burst window: Explorer starts the processes for a multi-file selection within a few
/// hundred milliseconds of each other.
constexpr int kFlushDelayMs = 400;

/// Per-user tag, so users on a shared machine (terminal server) do not share an instance.
QString userTag()
{
#ifdef Q_OS_WIN
    const QByteArray user = qgetenv("USERNAME") + '@' + qgetenv("USERDOMAIN");
#else
    const QByteArray user = qgetenv("USER");
#endif
    return QString::fromLatin1(QCryptographicHash::hash(user, QCryptographicHash::Sha1).toHex().left(12));
}

/// Directory for the socket and lock file, shared by every instance of this user.
QString runtimeDir()
{
#if defined(Q_OS_LINUX)
    // Flatpak: the per-app runtime directory is shared by all instances of the app.
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    const QString xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!flatpakId.isEmpty() && !xdg.isEmpty()) return xdg + QStringLiteral("/app/") + flatpakId;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!dir.isEmpty()) return dir;
#endif
    return QDir::tempPath();  // Windows and macOS: per-user temp folder
}

QString serverName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("io.github.doluk.AUCDataTool-") + userTag();  // a named pipe
#else
    return runtimeDir() + QStringLiteral("/aucdatatool-") + userTag() + QStringLiteral(".sock");
#endif
}

}  // namespace

#ifdef Q_OS_ANDROID
/// Tapping a file while the app is already running delivers ACTION_VIEW via onNewIntent
/// (the activity is singleTop), on the Android UI thread.
struct FileOpenRouter::IntentListener final : QtAndroidPrivate::NewIntentListener {
    explicit IntentListener(FileOpenRouter* router) : router(router) {}
    bool handleNewIntent(JNIEnv*, jobject intent) override
    {
        const QJniObject i(intent);
        const QJniObject data = i.callObjectMethod("getDataString", "()Ljava/lang/String;");
        const QString uri = data.isValid() ? data.toString() : QString();
        if (uri.isEmpty()) return false;
        QMetaObject::invokeMethod(router, [r = router, uri] { r->add({QUrl(uri)}); }, Qt::QueuedConnection);
        return true;
    }
    QPointer<FileOpenRouter> router;
};
#endif

FileOpenRouter::FileOpenRouter(QObject* parent)
    : QObject(parent)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushDelayMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &FileOpenRouter::flush);
#ifdef Q_OS_ANDROID
    m_intentListener = std::make_unique<IntentListener>(this);
    QtAndroidPrivate::registerNewIntentListener(m_intentListener.get());
#endif
}

FileOpenRouter::~FileOpenRouter()
{
#ifdef Q_OS_ANDROID
    QtAndroidPrivate::unregisterNewIntentListener(m_intentListener.get());
#endif
}

QString FileOpenRouter::lockFilePath()
{
    return runtimeDir() + QStringLiteral("/aucdatatool-") + userTag() + QStringLiteral(".lock");
}

bool FileOpenRouter::forwardToRunningInstance(const QStringList& files)
{
    if (files.isEmpty()) return false;
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(1000)) return false;
#ifdef Q_OS_WIN
    // Lets the running instance bring its window to the front.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    // One URL per line, an empty line ends the request; the server answers "ok".
    QByteArray msg;
    for (const QString& f : files) msg += QUrl::fromLocalFile(QFileInfo(f).absoluteFilePath()).toEncoded() + '\n';
    msg += '\n';
    socket.write(msg);
    if (!socket.waitForBytesWritten(2000)) return false;
    // No answer (instance hung or shutting down): open the files in a new window instead.
    while (!socket.canReadLine())
        if (!socket.waitForReadyRead(3000)) return false;
    const bool ok = socket.readLine().trimmed() == "ok";
    socket.disconnectFromServer();
    return ok;
}

bool FileOpenRouter::listen()
{
    if (m_server) return true;
    auto* server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = serverName();
    if (!server->listen(name)) {
        // The caller holds the lock and nobody answered: a socket left by a crashed instance.
        QLocalServer::removeServer(name);
        if (!server->listen(name)) {
            delete server;
            return false;
        }
    }
    m_server = server;
    connect(server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            auto urls = std::make_shared<QList<QUrl>>();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, urls] {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine().trimmed();
                    if (!line.isEmpty()) {
                        if (const QUrl u = QUrl::fromEncoded(line); u.isValid()) urls->append(u);
                        continue;
                    }
                    socket->write("ok\n");
                    socket->flush();
                    add(*urls);
                    urls->clear();
                    raiseWindow();
                }
            });
        }
    });
    return true;
}

void FileOpenRouter::setController(AppController* controller, QWindow* window)
{
    m_controller = controller;
    m_window = window;
    if (m_controller && !m_pending.isEmpty()) m_flushTimer.start();
}

void FileOpenRouter::add(const QList<QUrl>& urls)
{
    if (urls.isEmpty()) return;
    m_pending += urls;
    if (m_controller) m_flushTimer.start();  // restarts: waits for the end of the burst
}

void FileOpenRouter::flush()
{
    if (!m_controller || m_pending.isEmpty()) return;
    const QList<QUrl> urls = std::exchange(m_pending, {});
    m_controller->openFiles(urls);
}

void FileOpenRouter::raiseWindow()
{
    if (!m_window) return;
    m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

bool FileOpenRouter::eventFilter(QObject* watched, QEvent* event)
{
    // macOS: Finder double-click / "Open With", one event per file.
    if (event->type() == QEvent::FileOpen) {
        const auto* e = static_cast<const QFileOpenEvent*>(event);
        const QUrl url = !e->file().isEmpty() ? QUrl::fromLocalFile(e->file()) : e->url();
        if (url.isValid()) add({url});
        return true;
    }
    return QObject::eventFilter(watched, event);
}
