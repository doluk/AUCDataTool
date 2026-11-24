// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>

class AppController;
class QLocalServer;
class QWindow;

/// Collects the files to open in an interactive session from every source: the command line,
/// macOS open events, later launches of the app (single instance) and Android view intents.
/// Each burst is opened together, so selecting several scan files in a file manager (one
/// process per file on Windows) gives one openData() call, i.e. one channel per cell, in one
/// window.
class FileOpenRouter final : public QObject {
public:
    explicit FileOpenRouter(QObject* parent = nullptr);
    ~FileOpenRouter() override;

    /// Lock file that serialises "forward or become the server" among processes started at
    /// the same time (Explorer starts one per selected file).
    static QString lockFilePath();
    /// Hands `files` to an already running instance; true if it confirmed receipt.
    static bool forwardToRunningInstance(const QStringList& files);
    /// Accepts files from later launches. Call with the lock file held.
    bool listen();

    void setController(AppController* controller, QWindow* window);
    void add(const QList<QUrl>& urls);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void flush();
    void raiseWindow();

    QPointer<AppController> m_controller;
    QPointer<QWindow> m_window;
    QList<QUrl> m_pending;
    QTimer m_flushTimer;
    QLocalServer* m_server = nullptr;
#ifdef Q_OS_ANDROID
    struct IntentListener;
    std::unique_ptr<IntentListener> m_intentListener;
#endif
};
