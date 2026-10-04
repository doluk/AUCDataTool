// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppController.h"

#include "auc/Processing.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QTime>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

double defaultWavelength(const auc::ChannelSource& c)
{
    // 280 nm (protein) if measured, otherwise the middle of the range.
    if (!c.wavelengths.empty() && c.wavelengths.front() <= 280.0 && c.wavelengths.back() >= 280.0) return 280.0;
    return c.wavelengths.empty() ? 0.0 : c.wavelengths[c.wavelengths.size() / 2];
}

}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    connect(&m_future, &QFutureWatcher<Result>::finished, this, &AppController::onProcessed);
    connect(this, &AppController::optionsChanged, this, [this] { reprocess(true); });

    m_watcher.setNameFilters({QStringLiteral("*.auc"), QStringLiteral("*.mwrs"), QStringLiteral("*.mw"),
                              QStringLiteral("*.mwrs.xml")});
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(500);
    connect(&m_liveTimer, &QTimer::timeout, this, &AppController::liveUpdate);
    connect(&m_watcher, &auc::FolderWatcher::fileAdded, &m_liveTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &auc::FolderWatcher::fileChanged, &m_liveTimer, qOverload<>(&QTimer::start));
}

AppController::~AppController()
{
    m_future.waitForFinished();
}

// ---------------------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------------------

AppController::Entry* AppController::current()
{
    return (m_current >= 0 && m_current < m_entries.size()) ? &m_entries[m_current] : nullptr;
}
const AppController::Entry* AppController::current() const
{
    return (m_current >= 0 && m_current < m_entries.size()) ? &m_entries[m_current] : nullptr;
}
const auc::ChannelSource* AppController::currentSrc() const
{
    const Entry* e = current();
    return e ? e->src.get() : nullptr;
}

QString AppController::channelTitle(const auc::ChannelSource& c)
{
    return tr("Cell %1 · %2").arg(c.cell).arg(QLatin1Char(c.channel));
}

QVariantList AppController::channels() const
{
    QVariantList list;
    for (const Entry& e : m_entries) {
        const auto& c = *e.src;
        QVariantMap m;
        m.insert(QStringLiteral("title"), channelTitle(c));
        m.insert(QStringLiteral("run"), c.runId);
        m.insert(QStringLiteral("description"), c.description);
        QStringList parts;
        if (!c.description.isEmpty()) parts << c.description;
        parts << c.formatName;
        if (c.wavelengths.size() > 1)
            parts << tr("%1 λ").arg(c.wavelengths.size());
        else if (!c.wavelengths.empty())
            parts << tr("%1 nm").arg(c.wavelengths.front(), 0, 'f', 0);
        parts << tr("%n scan(s)", nullptr, int(c.scans.size()));
        parts << (c.absorbanceData ? tr("absorbance") : tr("intensity"));
        m.insert(QStringLiteral("subtitle"), parts.join(QStringLiteral(" · ")));
        list << m;
    }
    return list;
}

void AppController::addChannels(const auc::OpenResult& res, bool selectFirstNew)
{
    int firstNew = -1;
    for (const auto& ch : res.channels) {
        bool replaced = false;
        for (int i = 0; i < m_entries.size(); ++i) {
            auto& e = m_entries[i];
            if (e.src->folder == ch->folder && e.src->key() == ch->key() && e.src->format == ch->format
                && e.src->runId == ch->runId) {
                e.src = ch;
                if (!ch->wavelengths.empty())
                    e.view.wavelengthIndex = std::min(e.view.wavelengthIndex, ch->wavelengths.size() - 1);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            Entry e;
            e.src = ch;
            m_entries.push_back(std::move(e));
            if (firstNew < 0) firstNew = int(m_entries.size()) - 1;
        }
    }
    if (!res.warnings.isEmpty()) setStatus(res.warnings.first() + (res.warnings.size() > 1 ? tr(" (+%1 more)").arg(res.warnings.size() - 1) : QString()));
    emit channelsChanged();
    if (selectFirstNew && firstNew >= 0)
        setCurrentIndex(firstNew);
    else if (m_current < 0 && !m_entries.isEmpty())
        setCurrentIndex(0);
}

void AppController::openPaths(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    QElapsedTimer t;
    t.start();
    const auc::OpenResult res = auc::openData(paths);
    addChannels(res, true);
    if (res.warnings.isEmpty())
        setStatus(tr("Opened %n channel(s) in %1 ms", nullptr, int(res.channels.size())).arg(t.elapsed()));
}

void AppController::openFiles(const QList<QUrl>& urls)
{
    QStringList paths;
    for (const QUrl& u : urls) paths << u.toLocalFile();
    openPaths(paths);
}

void AppController::openFolder(const QUrl& url, bool watchLive)
{
    const QString dir = url.toLocalFile();
    openPaths({dir});
    if (watchLive) {
        m_watcher.watch(dir);
        emit liveChanged();
    }
}

void AppController::liveUpdate()
{
    if (!m_watcher.isActive()) return;
    const auc::ChannelPtr before = current() ? current()->src : nullptr;
    const auc::OpenResult res = auc::openData({m_watcher.folder()});
    addChannels(res, false);
    if (current() && current()->src != before) {
        emit datasetChanged();
        emit viewChanged();
        reprocess(true);  // keep the user's zoom during a run
    }
    setStatus(tr("Live update: %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
}

void AppController::stopLive()
{
    m_watcher.stop();
    emit liveChanged();
    setStatus(tr("Live update stopped"));
}

void AppController::closeAll()
{
    m_watcher.stop();
    emit liveChanged();
    m_entries.clear();
    m_current = -1;
    m_processed.reset();
    if (m_scanPlot) m_scanPlot->setSeries(nullptr);
    if (m_integralPlot) m_integralPlot->setSeries(nullptr);
    emit channelsChanged();
    emit currentIndexChanged();
    emit datasetChanged();
    emit viewChanged();
    emit noiseChanged();
}

void AppController::initSettings(int index)
{
    Entry& e = m_entries[index];
    if (e.view.initialised) return;
    const auto& c = *e.src;
    ChannelSettings v;
    v.initialised = true;
    const double wl = defaultWavelength(c);
    v.wavelengthIndex = c.nearestWavelength(wl);
    v.mwaFrom = wl - 2.0;
    v.mwaTo = wl + 2.0;
    v.darkSubtracted = c.darkSubtractedInFile;
    if (c.absorbanceData) {
        v.display = DisplayMode::Absorbance;
    } else {
        // Default reference: channel B of the same cell (AUC-Viewer convention: A/S sample, B reference).
        if (c.channel != 'B') {
            for (int i = 0; i < m_entries.size(); ++i) {
                const auto& o = *m_entries[i].src;
                if (i != index && o.folder == c.folder && o.cell == c.cell && o.channel == 'B' && !o.absorbanceData) {
                    v.reference = i;
                    break;
                }
            }
        }
        v.display = v.reference >= 0 ? DisplayMode::Absorbance : DisplayMode::Intensity;
    }
    e.view = v;
}

void AppController::setCurrentIndex(int i)
{
    if (i < -1 || i >= m_entries.size() || i == m_current) return;
    m_current = i;
    if (i >= 0) initSettings(i);
    emit currentIndexChanged();
    emit datasetChanged();
    emit viewChanged();
    emit noiseChanged();

    // Radius-dependent options: keep them if they fit the new channel.
    if (const auto* c = currentSrc(); c && !c->radius.empty()) {
        const double r0 = c->radius.front(), r1 = c->radius.back();
        auto outside = [&](double r) { return r < r0 || r > r1; };
        bool changed = false;
        if (outside(m_optOffsetR1)) m_optOffsetR1 = r1 - 0.05 * (r1 - r0), changed = true;
        if (outside(m_optOffsetR2)) m_optOffsetR2 = r1 - 0.01 * (r1 - r0), changed = true;
        if (outside(m_optIntR1)) m_optIntR1 = r0 + 0.3 * (r1 - r0), changed = true;
        if (outside(m_optIntR2)) m_optIntR2 = r0 + 0.9 * (r1 - r0), changed = true;
        if (m_optFirst >= int(c->scans.size())) m_optFirst = 0, changed = true;
        if (changed) emit optionsChanged();
    }
    reprocess(false);
}

// ---------------------------------------------------------------------------------------
// Current channel info
// ---------------------------------------------------------------------------------------

int AppController::scanCount() const { return currentSrc() ? int(currentSrc()->scans.size()) : 0; }
double AppController::radiusMin() const { return currentSrc() && !currentSrc()->radius.empty() ? currentSrc()->radius.front() : 0.0; }
double AppController::radiusMax() const { return currentSrc() && !currentSrc()->radius.empty() ? currentSrc()->radius.back() : 1.0; }
bool AppController::dataIsAbsorbance() const { return currentSrc() && currentSrc()->absorbanceData; }
bool AppController::hasDarkCurrent() const { return currentSrc() && !currentSrc()->darkCurrent.empty(); }
int AppController::wavelengthCount() const { return currentSrc() ? int(currentSrc()->wavelengths.size()) : 0; }
double AppController::wavelengthMin() const { return wavelengthCount() ? currentSrc()->wavelengths.front() : 0.0; }
double AppController::wavelengthMax() const { return wavelengthCount() ? currentSrc()->wavelengths.back() : 0.0; }

QString AppController::runInfo() const
{
    const Entry* e = current();
    if (!e || e->src->scans.empty()) return {};
    const auto& c = *e->src;
    const auto& a = c.scans.front();
    const auto& b = c.scans.back();
    const QString wl = e->view.mwa ? tr("%1–%2 nm").arg(e->view.mwaFrom, 0, 'f', 1).arg(e->view.mwaTo, 0, 'f', 1)
                                   : tr("%1 nm").arg(wavelength(), 0, 'f', 1);
    return tr("%1 · Cell %2%3 · %4 · %5 rpm · %6 °C · %7 scans · %8–%9 s")
        .arg(c.runId)
        .arg(c.cell)
        .arg(QLatin1Char(c.channel))
        .arg(wl)
        .arg(a.setRpm > 0 ? a.setRpm : a.rpm, 0, 'f', 0)
        .arg(a.temperature, 0, 'f', 1)
        .arg(c.scans.size())
        .arg(a.seconds, 0, 'f', 0)
        .arg(b.seconds, 0, 'f', 0);
}

QString AppController::yLabel() const
{
    const Entry* e = current();
    if (!e) return tr("Absorbance (OD)");
    if (e->src->absorbanceData || e->view.display == DisplayMode::Absorbance) return tr("Absorbance (OD)");
    return tr("Intensity (counts)");
}

// ---------------------------------------------------------------------------------------
// Per-channel view settings
// ---------------------------------------------------------------------------------------

int AppController::wavelengthIndex() const { return current() ? int(current()->view.wavelengthIndex) : 0; }

void AppController::setWavelengthIndex(int i)
{
    Entry* e = current();
    if (!e || e->src->wavelengths.empty()) return;
    const std::size_t idx = std::size_t(std::clamp(i, 0, int(e->src->wavelengths.size()) - 1));
    if (idx == e->view.wavelengthIndex) return;
    e->view.wavelengthIndex = idx;
    emit viewChanged();
    // Absorbance is comparable across wavelengths – keep the axes; intensity is not.
    reprocess(e->src->absorbanceData || e->view.display == DisplayMode::Absorbance);
}

void AppController::stepWavelength(int delta) { setWavelengthIndex(wavelengthIndex() + delta); }

double AppController::wavelength() const
{
    const auto* c = currentSrc();
    return c && !c->wavelengths.empty() ? c->wavelengths[std::min(current()->view.wavelengthIndex, c->wavelengths.size() - 1)] : 0.0;
}

bool AppController::mwa() const { return current() && current()->view.mwa; }
void AppController::setMwa(bool on)
{
    Entry* e = current();
    if (!e || e->view.mwa == on) return;
    e->view.mwa = on;
    emit viewChanged();
    reprocess(true);
}
double AppController::mwaFrom() const { return current() ? current()->view.mwaFrom : 0.0; }
void AppController::setMwaFrom(double nm)
{
    Entry* e = current();
    if (!e || e->view.mwaFrom == nm) return;
    e->view.mwaFrom = nm;
    emit viewChanged();
    if (e->view.mwa) reprocess(true);
}
double AppController::mwaTo() const { return current() ? current()->view.mwaTo : 0.0; }
void AppController::setMwaTo(double nm)
{
    Entry* e = current();
    if (!e || e->view.mwaTo == nm) return;
    e->view.mwaTo = nm;
    emit viewChanged();
    if (e->view.mwa) reprocess(true);
}

int AppController::displayMode() const { return current() ? int(current()->view.display) : 0; }
void AppController::setDisplayMode(int m)
{
    Entry* e = current();
    if (!e || e->src->absorbanceData) return;
    const auto mode = m == 1 ? DisplayMode::Absorbance : DisplayMode::Intensity;
    if (mode == e->view.display) return;
    e->view.display = mode;
    emit viewChanged();
    reprocess(false);  // different quantity → autoscale
}

QVariantList AppController::referenceChoices() const
{
    QVariantList list{tr("None")};
    for (const Entry& e : m_entries) {
        const auto& c = *e.src;
        QString t = channelTitle(c);
        if (!c.description.isEmpty()) t += QStringLiteral(" – ") + c.description;
        list << t;
    }
    return list;
}
int AppController::referenceChoice() const { return current() ? current()->view.reference + 1 : 0; }
void AppController::setReferenceChoice(int choice)
{
    Entry* e = current();
    if (!e) return;
    const int ref = std::clamp(choice - 1, -1, int(m_entries.size()) - 1);
    if (ref == e->view.reference) return;
    e->view.reference = ref;
    if (ref >= 0 && e->view.display == DisplayMode::Intensity && !e->src->absorbanceData)
        e->view.display = DisplayMode::Absorbance;  // choosing a reference means "show absorbance"
    emit viewChanged();
    reprocess(false);
}
int AppController::referenceMode() const { return current() ? int(current()->view.refMode) : 0; }
void AppController::setReferenceMode(int m)
{
    Entry* e = current();
    if (!e) return;
    const auto mode = m == 1 ? ReferenceMode::MeanOfScans : ReferenceMode::ScanByScan;
    if (mode == e->view.refMode) return;
    e->view.refMode = mode;
    emit viewChanged();
    reprocess(true);
}
int AppController::refFirst() const { return current() ? current()->view.refFirst : 0; }
void AppController::setRefFirst(int s)
{
    Entry* e = current();
    if (!e || s == e->view.refFirst) return;
    e->view.refFirst = std::max(0, s);
    emit viewChanged();
    reprocess(true);
}
int AppController::refLast() const { return current() ? current()->view.refLast : -1; }
void AppController::setRefLast(int s)
{
    Entry* e = current();
    if (!e || s == e->view.refLast) return;
    e->view.refLast = s;
    emit viewChanged();
    reprocess(true);
}
int AppController::referenceScanCount() const
{
    const Entry* e = current();
    if (!e || e->view.reference < 0 || e->view.reference >= m_entries.size()) return 0;
    return int(m_entries[e->view.reference].src->scans.size());
}
bool AppController::darkSubtracted() const { return current() && current()->view.darkSubtracted; }
void AppController::setDarkSubtracted(bool on)
{
    Entry* e = current();
    if (!e || on == e->view.darkSubtracted) return;
    e->view.darkSubtracted = on;
    emit viewChanged();
    reprocess(true);
}

// ---------------------------------------------------------------------------------------
// Processing
// ---------------------------------------------------------------------------------------

ProcessingOptions AppController::currentOptions() const
{
    ProcessingOptions o;
    o.reverse = m_optReverse;
    o.removeSpikes = m_optSpikes;
    o.offsetMode = ProcessingOptions::OffsetMode(std::clamp(m_optOffsetMode, 0, 2));
    o.offsetR1 = m_optOffsetR1;
    o.offsetR2 = m_optOffsetR2;
    o.firstScan = std::max(0, m_optFirst);
    o.lastScan = m_optLast;
    o.everyNth = std::max(1, m_optNth);
    o.integrate = m_optIntegrate;
    o.intR1 = m_optIntR1;
    o.intR2 = m_optIntR2;
    o.radialWeight = m_optRadialWeight;
    o.colormap = Colormap::Kind(std::clamp(m_optColormap, 0, 3));
    o.applyTi = m_optApplyTi;
    o.applyRi = m_optApplyRi;
    return o;
}

AppController::Result AppController::runProcessing(Job job)
{
    QElapsedTimer t;
    t.start();
    Result res;
    res.generation = job.generation;
    res.keepView = job.keepView;
    const auc::ChannelSource& src = *job.src;
    const ChannelSettings& v = job.view;
    QStringList warnings;

    // 1. Wavelength slice (or MWA mean) – the only file I/O, done here off the GUI thread.
    const double sampleWl = src.wavelengths.empty() ? 0.0 : src.wavelengths[std::min(v.wavelengthIndex, src.wavelengths.size() - 1)];
    auto slice = [&](const auc::ChannelSource& c, std::shared_ptr<const auc::Dataset>& out, double& dark) {
        std::size_t first, count;
        if (v.mwa) {
            first = c.nearestWavelength(std::min(v.mwaFrom, v.mwaTo));
            count = c.nearestWavelength(std::max(v.mwaFrom, v.mwaTo)) - first + 1;
        } else {
            first = (&c == &src) ? v.wavelengthIndex : c.nearestWavelength(sampleWl);
            count = 1;
        }
        dark = 0.0;
        if (!c.darkCurrent.empty()) {
            for (std::size_t k = first; k < first + count && k < c.darkCurrent.size(); ++k) dark += c.darkCurrent[k];
            dark /= double(count);
        }
        return c.wavelengthMean(first, count, out);
    };
    auto adjustDark = [&](auc::Dataset& d, const auc::ChannelSource& c, double dark) {
        if (c.darkCurrent.empty() || v.darkSubtracted == c.darkSubtractedInFile) return;
        const float delta = float(v.darkSubtracted ? -dark : dark);
        for (auto& s : d.scans)
            for (float& x : s.values) x += delta;
    };

    std::shared_ptr<const auc::Dataset> raw;
    double dark = 0.0;
    if (auc::IoResult r = slice(src, raw, dark); !r.ok()) {
        res.error = r.message;
        return res;
    }
    auto d = std::make_shared<auc::Dataset>(*raw);
    adjustDark(*d, src, dark);

    // 2. Absorbance against the reference channel.
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData) {
        if (!job.ref) {
            res.error = tr("Choose a reference channel to show absorbance.");
            return res;
        }
        std::shared_ptr<const auc::Dataset> rraw;
        double rdark = 0.0;
        if (auc::IoResult r = slice(*job.ref, rraw, rdark); !r.ok()) {
            res.error = tr("Reference: %1").arg(r.message);
            return res;
        }
        auc::Dataset rd = *rraw;
        adjustDark(rd, *job.ref, rdark);
        if (job.ref->absorbanceData) {
            res.error = tr("The reference channel contains absorbance, not intensity.");
            return res;
        }
        std::string err;
        if (v.refMode == ReferenceMode::ScanByScan) {
            const std::size_t before = d->scans.size();
            err = auc::proc::absorbanceScanByScan(*d, rd);
            if (err.empty() && d->scans.size() < before) warnings << tr("reference has only %1 scans").arg(d->scans.size());
        } else {
            const std::size_t last = v.refLast < 0 ? SIZE_MAX : std::size_t(v.refLast);
            err = auc::proc::absorbanceMeanReference(*d, rd, std::size_t(std::max(0, v.refFirst)), last);
        }
        if (!err.empty()) {
            res.error = tr("Reference: %1").arg(QString::fromStdString(err));
            return res;
        }
    }

    // 3. Noise (refers to scan indices and the radius grid of the file).
    if (job.opt.applyTi && job.ti)
        if (QString e = auc::noise::apply(*d, *job.ti); !e.isEmpty()) warnings << e;
    if (job.opt.applyRi && job.ri)
        if (QString e = auc::noise::apply(*d, *job.ri); !e.isEmpty()) warnings << e;

    // 4. Scan selection and corrections.
    const auto& o = job.opt;
    if (!d->scans.empty()) {
        const std::size_t last = o.lastScan < 0 ? d->scanCount() - 1 : std::size_t(o.lastScan);
        const auto idx = auc::proc::selectScans(d->scanCount(), std::size_t(o.firstScan), last, std::size_t(o.everyNth));
        std::vector<auc::Scan> kept;
        kept.reserve(idx.size());
        for (std::size_t i : idx) kept.push_back(std::move(d->scans[i]));
        d->scans = std::move(kept);
    }
    if (o.reverse) auc::proc::reverseRadius(*d);
    if (o.removeSpikes) auc::proc::removeSpikes(*d);
    if (o.offsetMode == ProcessingOptions::OffsetPoint) auc::proc::subtractOffsetAt(*d, o.offsetR1);
    if (o.offsetMode == ProcessingOptions::OffsetRegion) auc::proc::subtractBaselineRegion(*d, o.offsetR1, o.offsetR2);

    // 5. Plot data.
    auto s = std::make_shared<PlotSeries>();
    s->x.assign(d->radius.begin(), d->radius.end());
    const std::size_t n = d->scans.size();
    s->y.reserve(n);
    s->colors.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        s->y.push_back(d->scans[i].values);
        s->colors.push_back(Colormap::color(o.colormap, n > 1 ? double(i) / double(n - 1) : 0.0));
    }
    s->computeBounds();
    res.scans = s;

    if (o.integrate && n > 0) {
        const auto integrals = auc::proc::integrateScans(*d, o.intR1, o.intR2, o.radialWeight);
        auto is = std::make_shared<PlotSeries>();
        std::vector<float> y;
        for (std::size_t i = 0; i < n; ++i) {
            is->x.push_back(float(d->scans[i].seconds / 60.0));
            y.push_back(float(integrals[i]));
        }
        is->y.push_back(std::move(y));
        is->colors.push_back(QColor(0x1d, 0x4e, 0xd8));
        is->computeBounds();
        res.integral = is;
    }

    res.processed = d;
    res.warning = warnings.join(QStringLiteral("; "));
    res.ms = double(t.nsecsElapsed()) / 1e6;
    return res;
}

void AppController::reprocess(bool keepView)
{
    const Entry* e = current();
    if (!e) return;
    if (m_future.isRunning()) {
        m_pending = true;
        m_pendingKeepView = m_pendingKeepView && keepView;
        return;
    }
    Job job;
    job.src = e->src;
    job.view = e->view;
    if (e->view.reference >= 0 && e->view.reference < m_entries.size()) job.ref = m_entries[e->view.reference].src;
    job.opt = currentOptions();
    job.ti = e->ti;
    job.ri = e->ri;
    job.generation = ++m_generation;
    job.keepView = keepView;
    setBusy(true);
    m_future.setFuture(QtConcurrent::run(&AppController::runProcessing, std::move(job)));
}

void AppController::onProcessed()
{
    const Result r = m_future.result();
    setBusy(false);
    if (m_pending) {
        m_pending = false;
        const bool kv = m_pendingKeepView;
        m_pendingKeepView = true;
        reprocess(kv);
        if (m_future.isRunning()) return;  // a newer result is coming
    }
    if (r.error != m_processingError) {
        m_processingError = r.error;
        emit processingErrorChanged();
    }
    if (!r.error.isEmpty()) {
        setStatus(r.error);
        if (m_scanPlot) m_scanPlot->setSeries(nullptr);
        if (m_integralPlot) m_integralPlot->setSeries(nullptr);
        m_processed.reset();
        return;
    }
    if (!r.warning.isEmpty()) setStatus(tr("Note: %1").arg(r.warning));
    m_processed = r.processed;
    if (m_scanPlot) m_scanPlot->setSeries(r.scans, r.keepView);
    if (m_integralPlot) m_integralPlot->setSeries(r.integral, false);
    emit processed(r.ms);
}

void AppController::setScanPlot(ScanPlot* p)
{
    if (m_scanPlot == p) return;
    m_scanPlot = p;
    emit scanPlotChanged();
    reprocess(false);
}

void AppController::setIntegralPlot(ScanPlot* p)
{
    if (m_integralPlot == p) return;
    m_integralPlot = p;
    emit integralPlotChanged();
}

void AppController::setStatus(const QString& s)
{
    if (s == m_status) return;
    m_status = s;
    emit statusChanged();
}

void AppController::setBusy(bool b)
{
    if (b == m_busy) return;
    m_busy = b;
    emit busyChanged();
}

// ---------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------

bool AppController::exportCsv(const QUrl& url)
{
    if (!m_processed) return false;
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(tr("Export failed: %1").arg(f.errorString()));
        return false;
    }
    QTextStream ts(&f);
    const auto& d = *m_processed;
    ts << "# " << runInfo() << "\n# " << yLabel() << "\n";
    ts << "radius_cm";
    for (const auto& s : d.scans) ts << ",t=" << s.seconds << "s";
    ts << "\n";
    for (std::size_t j = 0; j < d.radius.size(); ++j) {
        ts << QString::number(d.radius[j], 'f', 5);
        for (const auto& s : d.scans) ts << ',' << (j < s.values.size() ? s.values[j] : 0.f);
        ts << "\n";
    }
    ts.flush();
    if (!f.commit()) {
        setStatus(tr("Export failed: %1").arg(f.errorString()));
        return false;
    }
    setStatus(tr("Exported %1 scans to %2").arg(d.scanCount()).arg(QFileInfo(url.toLocalFile()).fileName()));
    return true;
}

// ---------------------------------------------------------------------------------------
// Noise files
// ---------------------------------------------------------------------------------------

bool AppController::loadNoise(const QUrl& url, bool ti)
{
    Entry* e = current();
    if (!e) return false;
    const QString path = url.toLocalFile();
    auc::noise::NoiseVector n;
    const auto type = ti ? auc::noise::Type::TimeInvariant : auc::noise::Type::RadiallyInvariant;
    const auc::IoResult res = auc::noise::readNoiseFile(path, type, n);
    if (!res.ok()) {
        setStatus(tr("%1: %2").arg(QFileInfo(path).fileName(), res.message));
        return false;
    }
    const std::size_t count = n.values.size();
    if (ti) {
        e->ti = std::move(n);
        e->tiPath = path;
    } else {
        e->ri = std::move(n);
        e->riPath = path;
    }
    emit noiseChanged();
    setStatus(tr("Loaded %1 noise: %2 (%3 values)").arg(ti ? QStringLiteral("TI") : QStringLiteral("RI"), QFileInfo(path).fileName()).arg(count));
    reprocess(true);
    return true;
}

void AppController::clearNoise(bool ti)
{
    Entry* e = current();
    if (!e) return;
    if (ti) {
        e->ti.reset();
        e->tiPath.clear();
    } else {
        e->ri.reset();
        e->riPath.clear();
    }
    emit noiseChanged();
    reprocess(true);
}

QString AppController::tiNoiseName() const { return current() ? QFileInfo(current()->tiPath).fileName() : QString(); }
QString AppController::riNoiseName() const { return current() ? QFileInfo(current()->riPath).fileName() : QString(); }
