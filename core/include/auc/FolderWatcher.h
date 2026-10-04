#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace auc {

/// Watches a data folder (recursively, to `maxDepth`) for new or rewritten data files
/// while an experiment is running. Changes are debounced: acquisition software often
/// writes a file in several steps, so a file is reported only after its size and
/// modification time have been stable for `settleMs`.
class FolderWatcher : public QObject {
    Q_OBJECT
public:
    explicit FolderWatcher(QObject* parent = nullptr);

    void setNameFilters(const QStringList& filters);  ///< default: *.auc
    void setSettleTime(int ms);                        ///< default: 750 ms
    void setMaxDepth(int depth);                       ///< default: 3

    bool watch(const QString& folder);
    void stop();
    QString folder() const { return m_folder; }
    bool isActive() const { return !m_folder.isEmpty(); }

    /// All matching files currently known, sorted.
    QStringList files() const;

signals:
    void fileAdded(const QString& path);
    void fileChanged(const QString& path);
    void fileRemoved(const QString& path);

private:
    struct Stamp {
        qint64 size = -1;
        QDateTime modified;
        bool operator==(const Stamp&) const = default;
    };

    void scheduleScan();
    void scan();
    void collect(const QString& dir, int depth, QHash<QString, Stamp>& out, QStringList& dirs) const;

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QStringList m_filters{QStringLiteral("*.auc")};
    QString m_folder;
    int m_maxDepth = 3;
    QHash<QString, Stamp> m_known;    ///< reported state
    QHash<QString, Stamp> m_pending;  ///< last observed state of not-yet-stable files
};

}  // namespace auc
