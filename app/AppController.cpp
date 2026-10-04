// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppController.h"

#include "auc/Export.h"
#include "auc/Processing.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTime>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentRun>

#ifdef AUC_HAVE_PRINT
#include <QPrintDialog>
#include <QPrinter>
#endif

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

/// Up to `max` evenly spread indices of [0, n).
std::vector<std::size_t> pickIndices(std::size_t n, std::size_t max)
{
    std::vector<std::size_t> idx;
    if (n == 0) return idx;
    if (n <= max) {
        idx.resize(n);
        for (std::size_t i = 0; i < n; ++i) idx[i] = i;
        return idx;
    }
    for (std::size_t i = 0; i < max; ++i) idx.push_back(i * (n - 1) / (max - 1));
    return idx;
}

/// CSV: comment lines, header row, one column per scan.
bool writeCsv(const auc::Dataset& d, const QString& path, const QStringList& comments, const QString& xName, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    for (const QString& c : comments) ts << "# " << c << "\n";
    ts << xName;
    for (const auto& s : d.scans) ts << ",t=" << s.seconds << "s";
    ts << "\n";
    for (std::size_t j = 0; j < d.radius.size(); ++j) {
        ts << QString::number(d.radius[j], 'f', 5);
        for (const auto& s : d.scans) ts << ',' << (j < s.values.size() ? s.values[j] : 0.f);
        ts << "\n";
    }
    ts.flush();
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

/// "name.csv" → "name_280nm.csv" when several wavelengths go to separate files.
QString withWavelength(const QString& path, double nm)
{
    const QFileInfo fi(path);
    const QString tag = QStringLiteral("_%1nm").arg(auc::exporter::wavelengthTag(nm));
    return fi.dir().filePath(fi.completeBaseName() + tag + (fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix()));
}

/// One page: caption on top, image scaled to the remaining area, footer.
void paintPage(QPaintDevice* dev, const QImage& img, const QString& caption)
{
    QPainter p(dev);
    const QRect page = p.viewport();
    QFont f = p.font();
    f.setPointSizeF(10);
    p.setFont(f);
    const int line = p.fontMetrics().height();
    const QRect captionRect(page.left(), page.top(), page.width(), 2 * line);
    p.drawText(captionRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, caption);
    const QRect footer(page.left(), page.bottom() - line, page.width(), line);
    f.setPointSizeF(8);
    p.setFont(f);
    p.drawText(footer, Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("AUCDataTool %1 · %2").arg(QStringLiteral(PROJECT_VERSION),
                                                         QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    const QRect area(page.left(), captionRect.bottom() + line / 2, page.width(), footer.top() - captionRect.bottom() - line);
    if (img.isNull() || area.height() <= 0) return;
    const QSize sz = img.size().scaled(area.size(), Qt::KeepAspectRatio);
    const QRect target(area.left() + (area.width() - sz.width()) / 2, area.top(), sz.width(), sz.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, img);
}

}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    connect(&m_future, &QFutureWatcher<Result>::finished, this, &AppController::onProcessed);
    connect(&m_export, &QFutureWatcher<QString>::finished, this, [this] {
        QString msg = m_export.result();
        const bool ok = !msg.startsWith(QLatin1Char('!'));
        if (!ok) msg = tr("Export failed: %1").arg(msg.mid(1));
        setStatus(msg);
        emit exportingChanged();
        emit exportFinished(ok, msg);
    });
    connect(this, &AppController::optionsChanged, this, [this] { reprocess(true); });

    m_watcher.setNameFilters({QStringLiteral("*.auc"), QStringLiteral("*.mwrs"), QStringLiteral("*.mw"), QStringLiteral("*.mw?"),
                              QStringLiteral("*.mwrs.xml"), QStringLiteral("*.ra?"), QStringLiteral("*.ri?"),
                              QStringLiteral("*.ip?"), QStringLiteral("*.wa?"), QStringLiteral("*.wi?"), QStringLiteral("*.fi?")});
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(500);
    connect(&m_liveTimer, &QTimer::timeout, this, &AppController::liveUpdate);
    connect(&m_watcher, &auc::FolderWatcher::fileAdded, &m_liveTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &auc::FolderWatcher::fileChanged, &m_liveTimer, qOverload<>(&QTimer::start));
}

AppController::~AppController()
{
    m_future.waitForFinished();
    m_export.waitForFinished();
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
        parts << (!c.valueLabel.isEmpty() ? c.valueLabel.section(QLatin1Char(' '), 0, 0).toLower()
                  : c.absorbanceData          ? tr("absorbance")
                                              : tr("intensity"));
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
                && e.src->runId == ch->runId && e.src->rawType == ch->rawType && e.src->formatName == ch->formatName) {
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
    if (m_scanPlot) {
        m_scanPlot->setSeries(nullptr);
        m_scanPlot->setCurveStyles({});
    }
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
        if (c.channel != 'B' && c.channel != 'R' && c.valueLabel.isEmpty()) {
            for (int i = 0; i < m_entries.size(); ++i) {
                const auto& o = *m_entries[i].src;
                if (i != index && o.folder == c.folder && o.cell == c.cell && (o.channel == 'B' || o.channel == 'R')
                    && !o.absorbanceData && o.valueLabel.isEmpty() && o.format == c.format) {
                    v.reference = i;
                    break;
                }
            }
        }
        v.display = v.reference >= 0 ? DisplayMode::Absorbance : DisplayMode::Intensity;
    }
    if (!c.valueLabel.isEmpty()) {
        v.reference = -1;
        v.display = DisplayMode::Intensity;
    }
    e.view = v;
}

void AppController::setCurrentIndex(int i)
{
    if (i < -1 || i >= m_entries.size() || i == m_current) return;
    if (Entry* e = current(); e && m_scanPlot) e->view.curveStyles = m_scanPlot->curveStyles();
    m_current = i;
    if (i >= 0) initSettings(i);
    if (m_scanPlot) {
        m_scanPlot->setSelectedCurve(-1);
        m_scanPlot->setCurveStyles(i >= 0 ? m_entries[i].view.curveStyles : QHash<int, CurveStyle>{});
    }
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
        if (outside(m_optSpecR)) m_optSpecR = r0 + 0.6 * (r1 - r0), changed = true;
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
    if (!e->src->valueLabel.isEmpty()) return e->src->valueLabel;
    if (e->src->absorbanceData || e->view.display == DisplayMode::Absorbance) return tr("Absorbance (OD)");
    return tr("Intensity (counts)");
}

QString AppController::xLabel() const
{
    const auto* c = currentSrc();
    return c && c->xIsWavelength ? tr("Wavelength (nm)") : tr("Radius (cm)");
}

bool AppController::hasSpectra() const
{
    const auto* c = currentSrc();
    return c && c->wavelengths.size() > 1 && !c->xIsWavelength;
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

int AppController::wavelengthIndexOf(double nm) const
{
    const auto* c = currentSrc();
    return c ? int(c->nearestWavelength(nm)) : 0;
}

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
    if (!e || e->src->absorbanceData || !e->src->valueLabel.isEmpty()) return;
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
    o.spectrum = m_optSpectrum;
    o.specR = m_optSpecR;
    o.specWidth = std::max(0.0, m_optSpecWidth);
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
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()) {
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
    std::vector<int> scanIds;  // original scan index of each kept scan (curve identity)
    std::vector<std::size_t> selected;
    if (!d->scans.empty()) {
        const std::size_t last = o.lastScan < 0 ? d->scanCount() - 1 : std::size_t(o.lastScan);
        selected = auc::proc::selectScans(d->scanCount(), std::size_t(o.firstScan), last, std::size_t(o.everyNth));
        std::vector<auc::Scan> kept;
        kept.reserve(selected.size());
        for (std::size_t i : selected) kept.push_back(std::move(d->scans[i]));
        scanIds.assign(selected.begin(), selected.end());
        d->scans = std::move(kept);
    }
    if (o.reverse) auc::proc::reverseRadius(*d);
    if (o.removeSpikes) auc::proc::removeSpikes(*d);
    if (o.offsetMode == ProcessingOptions::OffsetPoint) auc::proc::subtractOffsetAt(*d, o.offsetR1);
    if (o.offsetMode == ProcessingOptions::OffsetRegion) auc::proc::subtractBaselineRegion(*d, o.offsetR1, o.offsetR2);

    res.scanIds = scanIds;
    if (!job.plots) {
        res.processed = d;
        res.warning = warnings.join(QStringLiteral("; "));
        return res;
    }

    // 5. Plot data.
    auto s = std::make_shared<PlotSeries>();
    s->x.assign(d->radius.begin(), d->radius.end());
    const std::size_t n = d->scans.size();
    s->y.reserve(n);
    s->colors.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        s->y.push_back(d->scans[i].values);
        s->colors.push_back(Colormap::color(o.colormap, n > 1 ? double(i) / double(n - 1) : 0.0));
        const int id = i < scanIds.size() ? scanIds[i] : int(i);
        s->ids.push_back(id);
        s->labels.push_back(tr("Scan %1 · %2 min").arg(id + 1).arg(d->scans[i].seconds / 60.0, 0, 'f', 1));
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

    // 6. Spectra at a radius and the 3D surface (both read whole scans).
    computeSpectrum(job, selected, res, warnings);
    if (job.surface) res.surface = computeSurface(job, *d, res.surfaceError);

    res.processed = d;
    res.warning = warnings.join(QStringLiteral("; "));
    res.ms = double(t.nsecsElapsed()) / 1e6;
    return res;
}

void AppController::computeSpectrum(const Job& job, const std::vector<std::size_t>& selected, Result& res, QStringList& warnings)
{
    const auc::ChannelSource& src = *job.src;
    const ChannelSettings& v = job.view;
    const ProcessingOptions& o = job.opt;
    if (!o.spectrum || src.wavelengths.size() < 2 || src.radius.empty() || src.xIsWavelength) return;

    auto window = [&](const auc::ChannelSource& c, std::size_t& first, std::size_t& count) {
        const std::size_t a = auc::proc::nearestIndex(c.radius, o.specR - o.specWidth);
        const std::size_t b = auc::proc::nearestIndex(c.radius, o.specR + o.specWidth);
        first = std::min(a, b);
        count = std::max(a, b) - first + 1;
    };
    auto adjustDark = [&](auc::Dataset& d, const auc::ChannelSource& c) {
        if (c.darkCurrent.empty() || v.darkSubtracted == c.darkSubtractedInFile) return;
        const float sign = v.darkSubtracted ? -1.f : 1.f;
        for (auto& sc : d.scans)
            for (std::size_t k = 0; k < sc.values.size() && k < c.darkCurrent.size(); ++k) sc.values[k] += sign * c.darkCurrent[k];
    };

    std::size_t first = 0, count = 1;
    window(src, first, count);
    std::shared_ptr<const auc::Dataset> raw;
    if (auc::IoResult r = src.spectra(first, count, raw); !r.ok()) {
        warnings << tr("spectrum: %1").arg(r.message);
        return;
    }
    auc::Dataset d = *raw;
    adjustDark(d, src);
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()) {
        if (!job.ref || job.ref->absorbanceData) return;  // reported by the main pipeline
        std::size_t rf = 0, rc = 1;
        window(*job.ref, rf, rc);
        std::shared_ptr<const auc::Dataset> rraw;
        if (auc::IoResult r = job.ref->spectra(rf, rc, rraw); !r.ok()) {
            warnings << tr("reference spectrum: %1").arg(r.message);
            return;
        }
        auc::Dataset rd = *rraw;
        adjustDark(rd, *job.ref);
        if (rd.radius != d.radius) {
            warnings << tr("spectrum: the reference channel has other wavelengths");
            return;
        }
        const std::string err = v.refMode == ReferenceMode::ScanByScan
            ? auc::proc::absorbanceScanByScan(d, rd)
            : auc::proc::absorbanceMeanReference(d, rd, std::size_t(std::max(0, v.refFirst)),
                                                 v.refLast < 0 ? SIZE_MAX : std::size_t(v.refLast));
        if (!err.empty()) {
            warnings << tr("spectrum: %1").arg(QString::fromStdString(err));
            return;
        }
    }

    auto s = std::make_shared<PlotSeries>();
    s->x.assign(d.radius.begin(), d.radius.end());
    std::size_t n = 0;
    for (std::size_t i : selected) n += i < d.scans.size();
    for (std::size_t i : selected) {
        if (i >= d.scans.size()) continue;
        const std::size_t k = s->y.size();
        s->y.push_back(d.scans[i].values);
        if (o.removeSpikes) s->y.back() = auc::proc::medianFilter(s->y.back(), 2, 0);
        s->colors.push_back(Colormap::color(o.colormap, n > 1 ? double(k) / double(n - 1) : 0.0));
        s->ids.push_back(int(i));
        s->labels.push_back(tr("Scan %1 · %2 min").arg(i + 1).arg(d.scans[i].seconds / 60.0, 0, 'f', 1));
    }
    s->computeBounds();
    res.spectrum = s;
}

SurfaceGridPtr AppController::computeSurface(const Job& job, const auc::Dataset& processed, QString& error)
{
    constexpr std::size_t kMaxCols = 400, kMaxRows = 300;
    const auc::ChannelSource& src = *job.src;
    const ChannelSettings& v = job.view;
    auto g = std::make_shared<SurfaceGrid>();
    g->yTitle = job.yLabel;
    g->xTitle = src.xIsWavelength ? tr("Wavelength (nm)") : tr("Radius (cm)");

    // Fills g->y from a row-major matrix [rows][np], picking rows/columns.
    auto fill = [&](const std::vector<std::size_t>& rows, const std::vector<std::size_t>& cols, auto&& value) {
        g->y.resize(rows.size() * cols.size());
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        for (std::size_t r = 0; r < rows.size(); ++r)
            for (std::size_t c = 0; c < cols.size(); ++c) {
                const float y = value(rows[r], cols[c]);
                g->y[r * cols.size() + c] = y;
                if (std::isfinite(y)) lo = std::min(lo, y), hi = std::max(hi, y);
            }
        if (lo > hi) lo = hi = 0.f;
        // Robust height range: single points (spikes, the 3.0 of invalid absorbance at the
        // cell edges) would otherwise flatten the surface. Values are clipped to the 0.5 %
        // and 99.5 % percentiles, widened by 5 %.
        std::vector<float> finite;
        finite.reserve(g->y.size());
        for (float y : g->y)
            if (std::isfinite(y)) finite.push_back(y);
        if (finite.size() > 20) {
            const auto at = [&](double q) {
                auto it = finite.begin() + std::ptrdiff_t(q * double(finite.size() - 1));
                std::nth_element(finite.begin(), it, finite.end());
                return *it;
            };
            const float p0 = at(0.005), p1 = at(0.995);
            const float pad = 0.05f * (p1 - p0);
            if (p1 > p0 && (p0 - pad > lo || p1 + pad < hi)) {
                lo = std::max(lo, p0 - pad);
                hi = std::min(hi, p1 + pad);
                g->clipped = true;
            }
        }
        for (float& y : g->y)
            y = std::isfinite(y) ? std::clamp(y, lo, hi) : lo;  // NaN: gaps outside a scan's radial range
        g->yMin = lo;
        g->yMax = hi > lo ? hi : lo + 1.f;
    };

    if (job.surfaceMode == SurfaceMode::RadiusTime) {
        if (processed.scans.size() < 2 || processed.radius.size() < 2) {
            error = tr("The surface needs at least two scans.");
            return nullptr;
        }
        const auto cols = pickIndices(processed.radius.size(), kMaxCols);
        const auto rows = pickIndices(processed.scans.size(), kMaxRows);
        for (std::size_t c : cols) g->x.push_back(float(processed.radius[c]));
        for (std::size_t r : rows) g->z.push_back(float(processed.scans[r].seconds / 60.0));
        g->zTitle = tr("Time (min)");
        fill(rows, cols, [&](std::size_t r, std::size_t c) {
            const auto& vals = processed.scans[r].values;
            return c < vals.size() ? vals[c] : std::numeric_limits<float>::quiet_NaN();
        });
        g->title = tr("%1 scans · %2").arg(processed.scans.size()).arg(processed.scans.empty() ? QString()
                                                                       : tr("%1 nm").arg(processed.scans.front().wavelength, 0, 'f', 1));
        if (g->clipped) g->title += tr(" · height clipped to 99 %");
        return g;
    }

    // Radius × wavelength for one scan.
    const std::size_t nwl = src.wavelengths.size(), np = src.radius.size();
    if (nwl < 2 || np < 2 || src.scans.empty()) {
        error = tr("Radius × wavelength needs multi-wavelength data.");
        return nullptr;
    }
    const std::size_t scan = job.surfaceScan < 0 ? src.scans.size() - 1 : std::min<std::size_t>(std::size_t(job.surfaceScan), src.scans.size() - 1);
    std::vector<float> m;
    if (auc::IoResult r = src.scanMatrix(scan, m); !r.ok()) {
        error = r.message;
        return nullptr;
    }
    auto adjustDark = [&](std::vector<float>& mat, const auc::ChannelSource& c) {
        if (c.darkCurrent.empty() || v.darkSubtracted == c.darkSubtractedInFile) return;
        const float sign = v.darkSubtracted ? -1.f : 1.f;
        for (std::size_t k = 0; k < nwl && k < c.darkCurrent.size(); ++k)
            for (std::size_t j = 0; j < np; ++j) mat[k * np + j] += sign * c.darkCurrent[k];
    };
    adjustDark(m, src);
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()) {
        const auc::ChannelSource* ref = job.ref.get();
        if (!ref || ref->absorbanceData) {
            error = tr("Choose a reference channel to show absorbance.");
            return nullptr;
        }
        if (ref->wavelengths.size() != nwl || ref->radius.size() != np) {
            error = tr("The reference channel has another wavelength/radius grid.");
            return nullptr;
        }
        std::vector<float> rm;
        if (v.refMode == ReferenceMode::ScanByScan) {
            if (scan >= ref->scans.size()) {
                error = tr("The reference has only %1 scans.").arg(ref->scans.size());
                return nullptr;
            }
            if (auc::IoResult r = ref->scanMatrix(scan, rm); !r.ok()) {
                error = r.message;
                return nullptr;
            }
        } else {
            const std::size_t a = std::min<std::size_t>(std::size_t(std::max(0, v.refFirst)), ref->scans.size() - 1);
            const std::size_t b = v.refLast < 0 ? ref->scans.size() - 1 : std::min<std::size_t>(std::size_t(v.refLast), ref->scans.size() - 1);
            std::vector<double> sum(nwl * np, 0.0);
            std::vector<float> one;
            std::size_t count = 0;
            for (std::size_t i = std::min(a, b); i <= std::max(a, b); ++i) {
                if (auc::IoResult r = ref->scanMatrix(i, one); !r.ok()) {
                    error = r.message;
                    return nullptr;
                }
                for (std::size_t q = 0; q < sum.size() && q < one.size(); ++q) sum[q] += one[q];
                ++count;
            }
            rm.resize(sum.size());
            for (std::size_t q = 0; q < sum.size(); ++q) rm[q] = float(sum[q] / double(std::max<std::size_t>(count, 1)));
        }
        adjustDark(rm, *ref);
        std::vector<float> a(nwl * np);
        for (std::size_t k = 0; k < nwl; ++k)
            auc::proc::absorbance(std::span<const float>(m).subspan(k * np, np), std::span<const float>(rm).subspan(k * np, np),
                                  std::span<float>(a).subspan(k * np, np));
        m = std::move(a);
    }
    const auto cols = pickIndices(np, kMaxCols);
    const auto rows = pickIndices(nwl, kMaxRows);
    for (std::size_t c : cols) g->x.push_back(float(src.radius[c]));
    for (std::size_t r : rows) g->z.push_back(float(src.wavelengths[r]));
    g->zTitle = tr("Wavelength (nm)");
    fill(rows, cols, [&](std::size_t r, std::size_t c) { return m[r * np + c]; });
    g->title = tr("Scan %1 · %2 min").arg(scan + 1).arg(src.scans[scan].seconds / 60.0, 0, 'f', 1);
    if (g->clipped) g->title += tr(" · height clipped to 99 %");
    return g;
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
    Job job = makeJob(*e);
    job.generation = ++m_generation;
    job.keepView = keepView;
    setBusy(true);
    m_future.setFuture(QtConcurrent::run(&AppController::runProcessing, std::move(job)));
}

AppController::Job AppController::makeJob(const Entry& e) const
{
    Job job;
    job.src = e.src;
    job.view = e.view;
    if (e.view.reference >= 0 && e.view.reference < m_entries.size()) job.ref = m_entries[e.view.reference].src;
    job.opt = currentOptions();
    job.opt.spectrum = job.opt.spectrum && m_spectrumPlot;
    job.ti = e.ti;
    job.ri = e.ri;
    job.surface = m_surfaceActive;
    job.surfaceMode = m_surfaceMode;
    job.surfaceScan = m_surfaceScan;
    job.yLabel = yLabel();
    return job;
}

QString AppController::spectrumKey() const
{
    // The spectrum plot keeps its zoom while only the radius window or options change.
    const Entry* e = current();
    if (!e) return {};
    return QStringLiteral("%1|%2|%3|%4").arg(quintptr(e->src.get())).arg(int(e->view.display)).arg(e->view.reference).arg(int(e->view.refMode));
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
        if (m_spectrumPlot) m_spectrumPlot->setSeries(nullptr);
        m_processed.reset();
        if (m_surface) {
            m_surface.reset();
            emit surfaceChanged();
        }
        return;
    }
    if (!r.warning.isEmpty()) setStatus(tr("Note: %1").arg(r.warning));
    m_processed = r.processed;
    if (m_scanPlot) m_scanPlot->setSeries(r.scans, r.keepView);
    if (m_integralPlot) m_integralPlot->setSeries(r.integral, false);
    if (m_spectrumPlot) {
        const QString key = spectrumKey();
        m_spectrumPlot->setSeries(r.spectrum, key == m_lastSpectrumKey && m_spectrumPlot->hasData());
        m_lastSpectrumKey = r.spectrum ? key : QString();
    }
    if (r.surface || m_surface) {
        m_surface = r.surface;
        emit surfaceChanged();
    }
    if (!r.surfaceError.isEmpty() && r.warning.isEmpty()) setStatus(r.surfaceError);
    emit processed(r.ms);
}

void AppController::setScanPlot(ScanPlot* p)
{
    if (m_scanPlot == p) return;
    m_scanPlot = p;
    emit scanPlotChanged();
    reprocess(false);
}

void AppController::setSpectrumPlot(ScanPlot* p)
{
    if (m_spectrumPlot == p) return;
    m_spectrumPlot = p;
    emit spectrumPlotChanged();
    if (m_optSpectrum) reprocess(true);
}

bool AppController::surfaceSupported()
{
#ifdef AUC_HAVE_GRAPHS
    return true;
#else
    return false;
#endif
}

void AppController::setSurfaceActive(bool on)
{
    if (on == m_surfaceActive) return;
    m_surfaceActive = on;
    emit surfaceSettingsChanged();
    if (on) reprocess(true);
}

void AppController::setSurfaceMode(int m)
{
    const auto mode = m == 1 ? SurfaceMode::RadiusTime : SurfaceMode::RadiusWavelength;
    if (mode == m_surfaceMode) return;
    m_surfaceMode = mode;
    emit surfaceSettingsChanged();
    if (m_surfaceActive) reprocess(true);
}

void AppController::setSurfaceScan(int s)
{
    s = std::max(-1, s);
    if (s == m_surfaceScan) return;
    m_surfaceScan = s;
    emit surfaceSettingsChanged();
    if (m_surfaceActive) reprocess(true);
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
    const auto& d = *m_processed;
    QString err;
    const auto* c = currentSrc();
    if (!writeCsv(d, url.toLocalFile(), {runInfo(), yLabel()}, c && c->xIsWavelength ? QStringLiteral("wavelength_nm") : QStringLiteral("radius_cm"), &err)) {
        setStatus(tr("Export failed: %1").arg(err));
        return false;
    }
    setStatus(tr("Exported %1 scans to %2").arg(d.scanCount()).arg(QFileInfo(url.toLocalFile()).fileName()));
    return true;
}

void AppController::exportData(int format, const QUrl& target, bool allWavelengths, double fromNm, double toNm, int step)
{
    const Entry* e = current();
    if (!e || m_export.isRunning()) return;
    const auto fmt = ExportFormat(std::clamp(format, 0, 3));
    Job base = makeJob(*e);
    base.plots = false;
    base.surface = false;
    base.opt.spectrum = false;
    base.opt.integrate = false;

    // Wavelength indices to export; SIZE_MAX = the current view (wavelength or MWA range).
    std::vector<std::size_t> indices;
    const auto& wl = e->src->wavelengths;
    if (allWavelengths && wl.size() > 1) {
        base.view.mwa = false;
        const double lo = std::min(fromNm, toNm), hi = std::max(fromNm, toNm);
        int n = 0;
        for (std::size_t k = 0; k < wl.size(); ++k)
            if (wl[k] >= lo - 1e-6 && wl[k] <= hi + 1e-6 && n++ % std::max(1, step) == 0) indices.push_back(k);
        if (indices.empty()) {
            setStatus(tr("Export: no wavelength in %1–%2 nm").arg(lo).arg(hi));
            return;
        }
    } else {
        indices.push_back(SIZE_MAX);
    }

    const QString path = target.toLocalFile();
    const QString run = e->src->runId;
    const QStringList header{runInfo(), yLabel()};
    const QString yName = yLabel(), xName = xLabel();
    const bool xWl = e->src->xIsWavelength;
    setStatus(tr("Exporting…"));
    m_export.setFuture(QtConcurrent::run([=]() -> QString {
        int files = 0;
        std::size_t scans = 0;
        for (std::size_t k : indices) {
            Job j = base;
            if (k != SIZE_MAX) j.view.wavelengthIndex = k;
            const Result r = runProcessing(j);
            if (!r.error.isEmpty()) return QStringLiteral("!") + r.error;
            const auc::Dataset& d = *r.processed;
            if (d.scans.empty()) continue;
            scans = d.scans.size();
            const double nm = d.scans.front().wavelength;
            const QString file = indices.size() > 1 ? withWavelength(path, nm) : path;
            QString err;
            switch (fmt) {
            case ExportFormat::Csv:
                if (!writeCsv(d, file, header, xWl ? QStringLiteral("wavelength_nm") : QStringLiteral("radius_cm"), &err))
                    return QStringLiteral("!") + err;
                ++files;
                break;
            case ExportFormat::Origin: {
                auto split = [](const QString& label, QString& name, QString& unit) {
                    const qsizetype p = label.lastIndexOf(QLatin1Char('('));
                    name = p > 0 ? label.left(p).trimmed() : label;
                    unit = p > 0 ? label.mid(p + 1).chopped(1) : QString();
                };
                auc::exporter::OriginColumns cols;
                split(xName, cols.xName, cols.xUnit);
                split(yName, cols.yName, cols.yUnit);
                for (std::size_t i = 0; i < d.scans.size(); ++i)
                    cols.comments.push_back(tr("scan %1, t = %2 s").arg(i < r.scanIds.size() ? r.scanIds[i] + 1 : int(i) + 1).arg(d.scans[i].seconds, 0, 'f', 0));
                if (auto res = auc::exporter::writeOrigin(d, file, cols); !res.ok()) return QStringLiteral("!") + res.message;
                ++files;
                break;
            }
            case ExportFormat::Beckman: {
                std::vector<int> numbers;
                for (int id : r.scanIds)
                    numbers.push_back(id >= 0 && std::size_t(id) < j.src->scans.size() ? j.src->scans[std::size_t(id)].number : id + 1);
                QStringList written;
                if (auto res = auc::exporter::writeBeckman(d, path, numbers, &written); !res.ok()) return QStringLiteral("!") + res.message;
                files += int(written.size());
                break;
            }
            case ExportFormat::UltraScan:
                if (auto res = auc::exporter::writeUs3(d, path, run); !res.ok()) return QStringLiteral("!") + res.message;
                ++files;
                break;
            }
        }
        return tr("Exported %n wavelength(s), %1 scans each, %2 file(s) to %3", nullptr, int(indices.size()))
            .arg(scans)
            .arg(files)
            .arg(QDir::toNativeSeparators(path));
    }));
    emit exportingChanged();
}

bool AppController::canPrint()
{
#ifdef AUC_HAVE_PRINT
    return true;
#else
    return false;
#endif
}

void AppController::printImage(const QVariant& image, const QString& caption)
{
#ifdef AUC_HAVE_PRINT
    const QImage img = image.value<QImage>();
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setDocName(caption);
    QPrintDialog dlg(&printer);
    dlg.setWindowTitle(tr("Print graph"));
    if (dlg.exec() != QDialog::Accepted) return;
    paintPage(&printer, img, caption);
    setStatus(tr("Printed"));
#else
    Q_UNUSED(image);
    Q_UNUSED(caption);
    setStatus(tr("Printing is not available in this build"));
#endif
}

bool AppController::saveImage(const QVariant& image, const QUrl& url, const QString& caption)
{
    const QImage img = image.value<QImage>();
    const QString path = url.toLocalFile();
    bool ok = false;
    if (QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        QPdfWriter pdf(path);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageOrientation(QPageLayout::Landscape);
        pdf.setResolution(300);
        pdf.setTitle(caption);
        pdf.setCreator(QStringLiteral("AUCDataTool"));
        paintPage(&pdf, img, caption);
        ok = QFileInfo::exists(path);
    } else {
        ok = img.save(path);
    }
    setStatus(ok ? tr("Saved graph to %1").arg(QFileInfo(path).fileName()) : tr("Could not save %1").arg(path));
    return ok;
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
