#pragma once

#include "Colormap.h"
#include "PlotSeries.h"

#include "auc/Dataset.h"
#include "auc/FolderWatcher.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "ScanPlot.h"

/// Processing options, applied in this order: reverse → spike filter → offset → scan selection.
struct ProcessingOptions {
    bool reverse = false;
    bool removeSpikes = false;
    enum OffsetMode { OffsetNone, OffsetPoint, OffsetRegion } offsetMode = OffsetNone;
    double offsetR1 = 0.0;  ///< point offset radius, or region start
    double offsetR2 = 0.0;  ///< region end
    int firstScan = 0;      ///< 0-based
    int lastScan = -1;      ///< -1 = last
    int everyNth = 1;
    bool integrate = false;
    double intR1 = 0.0, intR2 = 0.0;
    bool radialWeight = false;
    Colormap::Kind colormap = Colormap::Viridis;
};

/// Owns loaded datasets, runs processing off the GUI thread and feeds the plots.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(QString liveFolder READ liveFolder NOTIFY liveChanged)

    Q_PROPERTY(ScanPlot* scanPlot READ scanPlot WRITE setScanPlot NOTIFY scanPlotChanged)
    Q_PROPERTY(ScanPlot* integralPlot READ integralPlot WRITE setIntegralPlot NOTIFY integralPlotChanged)

    // Info about the current dataset
    Q_PROPERTY(int scanCount READ scanCount NOTIFY datasetChanged)
    Q_PROPERTY(double radiusMin READ radiusMin NOTIFY datasetChanged)
    Q_PROPERTY(double radiusMax READ radiusMax NOTIFY datasetChanged)
    Q_PROPERTY(QString runInfo READ runInfo NOTIFY datasetChanged)
    Q_PROPERTY(QString yLabel READ yLabel NOTIFY datasetChanged)

    // Processing options
    Q_PROPERTY(bool reverse MEMBER m_optReverse NOTIFY optionsChanged)
    Q_PROPERTY(bool removeSpikes MEMBER m_optSpikes NOTIFY optionsChanged)
    Q_PROPERTY(int offsetMode MEMBER m_optOffsetMode NOTIFY optionsChanged)
    Q_PROPERTY(double offsetR1 MEMBER m_optOffsetR1 NOTIFY optionsChanged)
    Q_PROPERTY(double offsetR2 MEMBER m_optOffsetR2 NOTIFY optionsChanged)
    Q_PROPERTY(int firstScan MEMBER m_optFirst NOTIFY optionsChanged)
    Q_PROPERTY(int lastScan MEMBER m_optLast NOTIFY optionsChanged)
    Q_PROPERTY(int everyNth MEMBER m_optNth NOTIFY optionsChanged)
    Q_PROPERTY(bool integrate MEMBER m_optIntegrate NOTIFY optionsChanged)
    Q_PROPERTY(double intR1 MEMBER m_optIntR1 NOTIFY optionsChanged)
    Q_PROPERTY(double intR2 MEMBER m_optIntR2 NOTIFY optionsChanged)
    Q_PROPERTY(bool radialWeight MEMBER m_optRadialWeight NOTIFY optionsChanged)
    Q_PROPERTY(int colormap MEMBER m_optColormap NOTIFY optionsChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QVariantList files() const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int i);
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool live() const { return m_watcher.isActive(); }
    QString liveFolder() const { return m_watcher.folder(); }

    ScanPlot* scanPlot() const { return m_scanPlot; }
    void setScanPlot(ScanPlot* p);
    ScanPlot* integralPlot() const { return m_integralPlot; }
    void setIntegralPlot(ScanPlot* p);

    int scanCount() const;
    double radiusMin() const;
    double radiusMax() const;
    QString runInfo() const;
    QString yLabel() const;

    Q_INVOKABLE void openFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void openFolder(const QUrl& url, bool watchLive);
    Q_INVOKABLE void stopLive();
    Q_INVOKABLE void closeAll();
    Q_INVOKABLE bool exportCsv(const QUrl& url);
    /// Loads files from plain paths (command line).
    void openPaths(const QStringList& paths);

signals:
    void filesChanged();
    void currentIndexChanged();
    void statusChanged();
    void busyChanged();
    void liveChanged();
    void scanPlotChanged();
    void integralPlotChanged();
    void datasetChanged();
    void optionsChanged();
    /// Emitted after a processing run delivered new plot data.
    void processed(double milliseconds);

private:
    struct Entry {
        QString path;
        std::shared_ptr<const auc::Dataset> data;  ///< null until loaded
        QString error;
    };
    struct Result {
        quint64 generation = 0;
        PlotSeriesPtr scans;
        PlotSeriesPtr integral;
        std::shared_ptr<const auc::Dataset> processed;
        double ms = 0.0;
        bool keepView = false;
    };

    void addPath(const QString& path, bool select);
    void loadEntry(int index);
    void reprocess(bool keepView);
    void onProcessed();
    void setStatus(const QString& s);
    void setBusy(bool b);
    ProcessingOptions currentOptions() const;
    static Result runProcessing(std::shared_ptr<const auc::Dataset> raw, ProcessingOptions opt, quint64 gen,
                                bool keepView);
    const auc::Dataset* currentData() const;

    QList<Entry> m_entries;
    int m_current = -1;
    QString m_status;
    bool m_busy = false;
    quint64 m_generation = 0;
    bool m_pending = false;      ///< options changed while a run was in flight
    bool m_pendingKeepView = true;
    QFutureWatcher<Result> m_future;
    std::shared_ptr<const auc::Dataset> m_processed;
    auc::FolderWatcher m_watcher;
    QPointer<ScanPlot> m_scanPlot;
    QPointer<ScanPlot> m_integralPlot;

    bool m_optReverse = false;
    bool m_optSpikes = false;
    int m_optOffsetMode = 0;
    double m_optOffsetR1 = 0.0, m_optOffsetR2 = 0.0;
    int m_optFirst = 0, m_optLast = -1, m_optNth = 1;
    bool m_optIntegrate = false;
    double m_optIntR1 = 0.0, m_optIntR2 = 0.0;
    bool m_optRadialWeight = false;
    int m_optColormap = Colormap::Viridis;
};
