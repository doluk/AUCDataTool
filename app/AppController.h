// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "Colormap.h"
#include "PlotSeries.h"
#include "ScanPlot.h"

#include "auc/Channel.h"
#include "auc/Dataset.h"
#include "auc/FolderWatcher.h"
#include "auc/Noise.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

/// What is shown for intensity data.
enum class DisplayMode { Intensity = 0, Absorbance = 1 };

/// How the reference intensity I0 is formed from the reference channel.
enum class ReferenceMode {
    ScanByScan = 0,  ///< sample scan i against reference scan i (LabVIEW: reference channel)
    MeanOfScans = 1, ///< against the mean of reference scans [first, last] (LabVIEW: reference scans)
};

/// Per-channel view settings, remembered when switching channels.
struct ChannelSettings {
    std::size_t wavelengthIndex = 0;
    bool mwa = false;           ///< average over a wavelength range
    double mwaFrom = 0, mwaTo = 0;
    DisplayMode display = DisplayMode::Intensity;
    int reference = -1;         ///< channel index, -1 = none
    ReferenceMode refMode = ReferenceMode::ScanByScan;
    int refFirst = 0, refLast = -1;
    bool darkSubtracted = false;
    bool initialised = false;
};

/// Processing options shared by all channels. Order of application: wavelength slice →
/// dark current → absorbance vs. reference → noise → scan selection → reverse → spike
/// filter → offset → integration.
struct ProcessingOptions {
    bool reverse = false;
    bool removeSpikes = false;
    enum OffsetMode { OffsetNone, OffsetPoint, OffsetRegion } offsetMode = OffsetNone;
    double offsetR1 = 0.0, offsetR2 = 0.0;
    int firstScan = 0, lastScan = -1, everyNth = 1;
    bool integrate = false;
    double intR1 = 0.0, intR2 = 0.0;
    bool radialWeight = false;
    Colormap::Kind colormap = Colormap::Viridis;
    bool applyTi = true, applyRi = true;
};

/// Owns the opened channels, runs processing off the GUI thread and feeds the plots.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(QString liveFolder READ liveFolder NOTIFY liveChanged)

    Q_PROPERTY(ScanPlot* scanPlot READ scanPlot WRITE setScanPlot NOTIFY scanPlotChanged)
    Q_PROPERTY(ScanPlot* integralPlot READ integralPlot WRITE setIntegralPlot NOTIFY integralPlotChanged)

    // Current channel
    Q_PROPERTY(int scanCount READ scanCount NOTIFY datasetChanged)
    Q_PROPERTY(double radiusMin READ radiusMin NOTIFY datasetChanged)
    Q_PROPERTY(double radiusMax READ radiusMax NOTIFY datasetChanged)
    Q_PROPERTY(QString runInfo READ runInfo NOTIFY viewChanged)
    Q_PROPERTY(QString yLabel READ yLabel NOTIFY viewChanged)
    Q_PROPERTY(bool dataIsAbsorbance READ dataIsAbsorbance NOTIFY datasetChanged)
    Q_PROPERTY(bool hasDarkCurrent READ hasDarkCurrent NOTIFY datasetChanged)

    // Wavelength (per channel)
    Q_PROPERTY(int wavelengthCount READ wavelengthCount NOTIFY datasetChanged)
    Q_PROPERTY(double wavelengthMin READ wavelengthMin NOTIFY datasetChanged)
    Q_PROPERTY(double wavelengthMax READ wavelengthMax NOTIFY datasetChanged)
    Q_PROPERTY(int wavelengthIndex READ wavelengthIndex WRITE setWavelengthIndex NOTIFY viewChanged)
    Q_PROPERTY(double wavelength READ wavelength NOTIFY viewChanged)
    Q_PROPERTY(bool mwa READ mwa WRITE setMwa NOTIFY viewChanged)
    Q_PROPERTY(double mwaFrom READ mwaFrom WRITE setMwaFrom NOTIFY viewChanged)
    Q_PROPERTY(double mwaTo READ mwaTo WRITE setMwaTo NOTIFY viewChanged)

    // Reference / display (per channel)
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY viewChanged)
    Q_PROPERTY(QVariantList referenceChoices READ referenceChoices NOTIFY channelsChanged)
    Q_PROPERTY(int referenceChoice READ referenceChoice WRITE setReferenceChoice NOTIFY viewChanged)
    Q_PROPERTY(int referenceMode READ referenceMode WRITE setReferenceMode NOTIFY viewChanged)
    Q_PROPERTY(int refFirst READ refFirst WRITE setRefFirst NOTIFY viewChanged)
    Q_PROPERTY(int refLast READ refLast WRITE setRefLast NOTIFY viewChanged)
    Q_PROPERTY(int referenceScanCount READ referenceScanCount NOTIFY viewChanged)
    Q_PROPERTY(bool darkSubtracted READ darkSubtracted WRITE setDarkSubtracted NOTIFY viewChanged)
    Q_PROPERTY(QString processingError READ processingError NOTIFY processingErrorChanged)

    // Processing options (all channels)
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
    Q_PROPERTY(bool applyTiNoise MEMBER m_optApplyTi NOTIFY optionsChanged)
    Q_PROPERTY(bool applyRiNoise MEMBER m_optApplyRi NOTIFY optionsChanged)

    // Noise files of the current channel
    Q_PROPERTY(QString tiNoiseName READ tiNoiseName NOTIFY noiseChanged)
    Q_PROPERTY(QString riNoiseName READ riNoiseName NOTIFY noiseChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    QVariantList channels() const;
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
    bool dataIsAbsorbance() const;
    bool hasDarkCurrent() const;

    int wavelengthCount() const;
    double wavelengthMin() const;
    double wavelengthMax() const;
    int wavelengthIndex() const;
    void setWavelengthIndex(int i);
    double wavelength() const;
    bool mwa() const;
    void setMwa(bool on);
    double mwaFrom() const;
    void setMwaFrom(double nm);
    double mwaTo() const;
    void setMwaTo(double nm);

    int displayMode() const;
    void setDisplayMode(int m);
    QVariantList referenceChoices() const;
    int referenceChoice() const;
    void setReferenceChoice(int c);
    int referenceMode() const;
    void setReferenceMode(int m);
    int refFirst() const;
    void setRefFirst(int s);
    int refLast() const;
    void setRefLast(int s);
    int referenceScanCount() const;
    bool darkSubtracted() const;
    void setDarkSubtracted(bool on);
    QString processingError() const { return m_processingError; }

    QString tiNoiseName() const;
    QString riNoiseName() const;

    Q_INVOKABLE void openFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void openFolder(const QUrl& url, bool watchLive);
    Q_INVOKABLE void stopLive();
    Q_INVOKABLE void closeAll();
    Q_INVOKABLE bool exportCsv(const QUrl& url);
    Q_INVOKABLE bool loadNoise(const QUrl& url, bool ti);
    Q_INVOKABLE void clearNoise(bool ti);
    Q_INVOKABLE void stepWavelength(int delta);
    /// Opens files/folders given as plain paths (command line).
    void openPaths(const QStringList& paths);

signals:
    void channelsChanged();
    void currentIndexChanged();
    void statusChanged();
    void busyChanged();
    void liveChanged();
    void scanPlotChanged();
    void integralPlotChanged();
    void datasetChanged();
    void viewChanged();
    void optionsChanged();
    void noiseChanged();
    void processingErrorChanged();
    void processed(double milliseconds);

private:
    struct Entry {
        auc::ChannelPtr src;
        ChannelSettings view;
        std::optional<auc::noise::NoiseVector> ti, ri;
        QString tiPath, riPath;
    };

    /// Everything the worker needs – copied, so the GUI may change state meanwhile.
    struct Job {
        auc::ChannelPtr src, ref;
        ChannelSettings view;
        ProcessingOptions opt;
        std::optional<auc::noise::NoiseVector> ti, ri;
        quint64 generation = 0;
        bool keepView = false;
    };
    struct Result {
        quint64 generation = 0;
        PlotSeriesPtr scans, integral;
        std::shared_ptr<const auc::Dataset> processed;
        double ms = 0.0;
        bool keepView = false;
        QString error;      ///< processing could not run (nothing shown)
        QString warning;    ///< ran, but something was skipped (noise, scans)
    };

    void addChannels(const auc::OpenResult& res, bool selectFirstNew);
    void initSettings(int index);
    void reprocess(bool keepView);
    void onProcessed();
    void liveUpdate();
    void setStatus(const QString& s);
    void setBusy(bool b);
    ProcessingOptions currentOptions() const;
    static Result runProcessing(Job job);
    Entry* current();
    const Entry* current() const;
    const auc::ChannelSource* currentSrc() const;
    static QString channelTitle(const auc::ChannelSource& c);

    QList<Entry> m_entries;
    int m_current = -1;
    QString m_status;
    QString m_processingError;
    bool m_busy = false;
    quint64 m_generation = 0;
    bool m_pending = false;
    bool m_pendingKeepView = true;
    QFutureWatcher<Result> m_future;
    std::shared_ptr<const auc::Dataset> m_processed;
    auc::FolderWatcher m_watcher;
    QTimer m_liveTimer;
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
    bool m_optApplyTi = true;
    bool m_optApplyRi = true;
};
