// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/FolderWatcher.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace auc {

FolderWatcher::FolderWatcher(QObject* parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(750);
    connect(&m_settle, &QTimer::timeout, this, &FolderWatcher::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FolderWatcher::scheduleScan);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FolderWatcher::scheduleScan);
}

void FolderWatcher::setNameFilters(const QStringList& filters) { m_filters = filters; }
void FolderWatcher::setSettleTime(int ms) { m_settle.setInterval(ms); }
void FolderWatcher::setMaxDepth(int depth) { m_maxDepth = std::max(depth, 0); }

bool FolderWatcher::watch(const QString& folder)
{
    stop();
    const QFileInfo fi(folder);
    if (!fi.isDir()) return false;
    m_folder = fi.absoluteFilePath();

    QStringList dirs;
    collect(m_folder, 0, m_known, dirs);
    m_watcher.addPaths(dirs);
    if (!m_known.isEmpty()) m_watcher.addPaths(m_known.keys());
    return true;
}

void FolderWatcher::stop()
{
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    m_settle.stop();
    m_known.clear();
    m_pending.clear();
    m_folder.clear();
}

QStringList FolderWatcher::files() const
{
    QStringList list = m_known.keys();
    std::sort(list.begin(), list.end());
    return list;
}

void FolderWatcher::scheduleScan()
{
    m_settle.start();  // restart: keep waiting while writes continue
}

void FolderWatcher::collect(const QString& dir, int depth, QHash<QString, Stamp>& out, QStringList& dirs) const
{
    dirs << dir;
    const QDir d(dir);
    for (const QFileInfo& fi : d.entryInfoList(m_filters, QDir::Files | QDir::Readable)) {
        out.insert(fi.absoluteFilePath(), Stamp{fi.size(), fi.lastModified()});
    }
    if (depth >= m_maxDepth) return;
    for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable))
        collect(sub.absoluteFilePath(), depth + 1, out, dirs);
}

void FolderWatcher::scan()
{
    if (m_folder.isEmpty()) return;

    QHash<QString, Stamp> now;
    QStringList dirs;
    collect(m_folder, 0, now, dirs);

    // New sub-directories (e.g. a new run folder) must be watched too.
    const QStringList watchedDirs = m_watcher.directories();
    for (const QString& d : dirs)
        if (!watchedDirs.contains(d)) m_watcher.addPath(d);

    bool unstable = false;
    for (auto it = now.cbegin(); it != now.cend(); ++it) {
        const QString& path = it.key();
        const Stamp& stamp = it.value();
        const auto known = m_known.constFind(path);
        if (known != m_known.cend() && *known == stamp) {
            m_pending.remove(path);
            continue;
        }
        // Report only once two consecutive scans see the same stamp.
        const auto pend = m_pending.constFind(path);
        if (pend == m_pending.cend() || !(*pend == stamp)) {
            m_pending.insert(path, stamp);
            unstable = true;
            continue;
        }
        m_pending.remove(path);
        const bool added = known == m_known.cend();
        m_known.insert(path, stamp);
        if (added) {
            m_watcher.addPath(path);
            emit fileAdded(path);
        } else {
            emit fileChanged(path);
        }
    }

    for (auto it = m_known.begin(); it != m_known.end();) {
        if (!now.contains(it.key())) {
            const QString path = it.key();
            it = m_known.erase(it);
            m_pending.remove(path);
            emit fileRemoved(path);
        } else {
            ++it;
        }
    }

    if (unstable) m_settle.start();
}

}  // namespace auc
