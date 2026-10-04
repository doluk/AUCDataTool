#include "AppController.h"

#include "ScanPlot.h"

#include "auc/AucFile.h"
#include "auc/Processing.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    connect(&m_future, &QFutureWatcher<Result>::finished, this, &AppController::onProcessed);
    connect(this, &AppController::optionsChanged, this, [this] { reprocess(true); });

    connect(&m_watcher, &auc::FolderWatcher::fileAdded, this, [this](const QString& p) {
        addPath(p, m_entries.isEmpty());
        setStatus(tr("New file: %1").arg(QFileInfo(p).fileName()));
    });
    connect(&m_watcher, &auc::FolderWatcher::fileChanged, this, [this](const QString& p) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].path != p) continue;
            m_entries[i].data.reset();
            if (i == m_current) {
                loadEntry(i);
                reprocess(true);  // live update keeps the user's zoom
            }
            emit filesChanged();
            setStatus(tr("Updated: %1").arg(QFileInfo(p).fileName()));
        }
    });
}

AppController::~AppController()
{
    m_future.waitForFinished();
}

// ---------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------

QVariantList AppController::files() const
{
    QVariantList list;
    for (const Entry& e : m_entries) {
        QVariantMap m;
        const QFileInfo fi(e.path);
        m.insert(QStringLiteral("path"), e.path);
        m.insert(QStringLiteral("name"), fi.fileName());
        m.insert(QStringLiteral("folder"), fi.dir().dirName());
        m.insert(QStringLiteral("error"), e.error);
        if (e.data) {
            m.insert(QStringLiteral("triple"), QString::fromStdString(e.data->tripleName()));
            m.insert(QStringLiteral("type"), QString::fromStdString(auc::toCode(e.data->type)));
            m.insert(QStringLiteral("scans"), int(e.data->scanCount()));
        } else {
            auc::AucFile::Header h;
            if (auc::AucFile::readHeader(e.path, h).ok()) {
                m.insert(QStringLiteral("triple"), QStringLiteral("%1%2").arg(h.cell).arg(h.channel));
                m.insert(QStringLiteral("type"), QString::fromStdString(auc::toCode(h.type)));
                m.insert(QStringLiteral("scans"), h.scanCount);
            }
        }
        list << m;
    }
    return list;
}

void AppController::addPath(const QString& path, bool select)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == abs) {
            if (select) setCurrentIndex(i);
            return;
        }
    }
    m_entries.push_back({abs, nullptr, {}});
    emit filesChanged();
    if (select) setCurrentIndex(int(m_entries.size()) - 1);
}

void AppController::openPaths(const QStringList& paths)
{
    bool first = true;
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            openFolder(QUrl::fromLocalFile(fi.absoluteFilePath()), false);
        } else {
            addPath(p, first);
        }
        first = false;
    }
}

void AppController::openFiles(const QList<QUrl>& urls)
{
    bool first = true;
    for (const QUrl& u : urls) {
        addPath(u.toLocalFile(), first);
        first = false;
    }
}

void AppController::openFolder(const QUrl& url, bool watchLive)
{
    const QString dir = url.toLocalFile();
    auc::FolderWatcher scan;  // reuse the recursive collector for the one-off listing
    if (!scan.watch(dir)) {
        setStatus(tr("Cannot open folder %1").arg(dir));
        return;
    }
    const QStringList found = scan.files();
    scan.stop();
    const bool selectFirst = m_entries.isEmpty();
    for (const QString& f : found) addPath(f, false);
    if (selectFirst && !m_entries.isEmpty()) setCurrentIndex(0);
    setStatus(tr("%n file(s) in %1", nullptr, int(found.size())).arg(QDir(dir).dirName()));

    if (watchLive) {
        m_watcher.watch(dir);
        emit liveChanged();
    }
}

void AppController::stopLive()
{
    m_watcher.stop();
    emit liveChanged();
    setStatus(tr("Live update stopped"));
}

void AppController::closeAll()
{
    stopLive();
    m_entries.clear();
    m_current = -1;
    m_processed.reset();
    if (m_scanPlot) m_scanPlot->setSeries(nullptr);
    if (m_integralPlot) m_integralPlot->setSeries(nullptr);
    emit filesChanged();
    emit currentIndexChanged();
    emit datasetChanged();
}

void AppController::setCurrentIndex(int i)
{
    if (i < -1 || i >= m_entries.size()) return;
    if (i == m_current && (i < 0 || m_entries[i].data)) return;
    m_current = i;
    emit currentIndexChanged();
    if (i < 0) return;
    loadEntry(i);
    emit datasetChanged();

    // Sensible defaults for radius-dependent options on a new dataset.
    if (const auc::Dataset* d = currentData(); d && !d->radius.empty()) {
        const double r0 = d->radius.front(), r1 = d->radius.back();
        auto outside = [&](double r) { return r < r0 || r > r1; };
        if (outside(m_optOffsetR1)) m_optOffsetR1 = r1 - 0.05 * (r1 - r0);
        if (outside(m_optOffsetR2)) m_optOffsetR2 = r1 - 0.01 * (r1 - r0);
        if (outside(m_optIntR1)) m_optIntR1 = r0 + 0.3 * (r1 - r0);
        if (outside(m_optIntR2)) m_optIntR2 = r0 + 0.9 * (r1 - r0);
        m_optLast = -1;
        m_optFirst = 0;
        emit optionsChanged();  // triggers reprocess
    }
    reprocess(false);
}

void AppController::loadEntry(int index)
{
    Entry& e = m_entries[index];
    if (e.data) return;
    auto d = std::make_shared<auc::Dataset>();
    QElapsedTimer t;
    t.start();
    const auc::IoResult res = auc::AucFile::read(e.path, *d);
    if (!res.ok()) {
        e.error = res.message;
        setStatus(tr("%1: %2").arg(QFileInfo(e.path).fileName(), res.message));
        return;
    }
    e.error.clear();
    e.data = std::move(d);
    setStatus(tr("Loaded %1 (%2 scans × %3 points) in %4 ms")
                  .arg(QFileInfo(e.path).fileName())
                  .arg(e.data->scanCount())
                  .arg(e.data->pointCount())
                  .arg(t.elapsed()));
}

const auc::Dataset* AppController::currentData() const
{
    return (m_current >= 0 && m_current < m_entries.size()) ? m_entries[m_current].data.get() : nullptr;
}

int AppController::scanCount() const
{
    const auto* d = currentData();
    return d ? int(d->scanCount()) : 0;
}
double AppController::radiusMin() const
{
    const auto* d = currentData();
    return d && !d->radius.empty() ? d->radius.front() : 0.0;
}
double AppController::radiusMax() const
{
    const auto* d = currentData();
    return d && !d->radius.empty() ? d->radius.back() : 1.0;
}

QString AppController::runInfo() const
{
    const auto* d = currentData();
    if (!d || d->scans.empty()) return {};
    const auto& a = d->scans.front();
    const auto& b = d->scans.back();
    return tr("Cell %1%2 · %3 nm · %4 rpm · %5 °C · %6 scans · %7–%8 s")
        .arg(d->cell)
        .arg(d->channel)
        .arg(a.wavelength, 0, 'f', 1)
        .arg(a.rpm, 0, 'f', 0)
        .arg(a.temperature, 0, 'f', 1)
        .arg(d->scanCount())
        .arg(a.seconds, 0, 'f', 0)
        .arg(b.seconds, 0, 'f', 0);
}

QString AppController::yLabel() const
{
    const auto* d = currentData();
    if (!d) return tr("Absorbance (OD)");
    switch (d->type) {
    case auc::DataType::RadialIntensity:
    case auc::DataType::WavelengthIntensity: return tr("Intensity (counts)");
    case auc::DataType::Interference: return tr("Fringes");
    case auc::DataType::Fluorescence: return tr("Fluorescence (a.u.)");
    default: return tr("Absorbance (OD)");
    }
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
    return o;
}

AppController::Result AppController::runProcessing(std::shared_ptr<const auc::Dataset> raw, ProcessingOptions o,
                                                   quint64 gen, bool keepView)
{
    QElapsedTimer t;
    t.start();
    Result res;
    res.generation = gen;
    res.keepView = keepView;

    auto d = std::make_shared<auc::Dataset>();
    // Copy only the selected scans first: everything after is linear in the shown data.
    d->type = raw->type;
    d->cell = raw->cell;
    d->channel = raw->channel;
    d->guid = raw->guid;
    d->description = raw->description;
    d->radius = raw->radius;
    const std::size_t last = o.lastScan < 0 ? raw->scanCount() - 1 : std::size_t(o.lastScan);
    const auto idx = auc::proc::selectScans(raw->scanCount(), std::size_t(o.firstScan), last, std::size_t(o.everyNth));
    d->scans.reserve(idx.size());
    for (std::size_t i : idx) d->scans.push_back(raw->scans[i]);

    if (o.reverse) auc::proc::reverseRadius(*d);
    if (o.removeSpikes) auc::proc::removeSpikes(*d);
    if (o.offsetMode == ProcessingOptions::OffsetPoint) auc::proc::subtractOffsetAt(*d, o.offsetR1);
    if (o.offsetMode == ProcessingOptions::OffsetRegion) auc::proc::subtractBaselineRegion(*d, o.offsetR1, o.offsetR2);

    auto s = std::make_shared<PlotSeries>();
    s->x.assign(d->radius.begin(), d->radius.end());
    s->y.reserve(d->scans.size());
    s->colors.reserve(d->scans.size());
    const std::size_t n = d->scans.size();
    for (std::size_t i = 0; i < n; ++i) {
        s->y.push_back(d->scans[i].values);
        s->colors.push_back(Colormap::color(o.colormap, n > 1 ? double(i) / double(n - 1) : 0.0));
    }
    s->computeBounds();
    res.scans = s;

    if (o.integrate && !d->scans.empty()) {
        const auto integrals = auc::proc::integrateScans(*d, o.intR1, o.intR2, o.radialWeight);
        auto is = std::make_shared<PlotSeries>();
        is->x.reserve(n);
        std::vector<float> y;
        y.reserve(n);
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
    res.ms = double(t.nsecsElapsed()) / 1e6;
    return res;
}

void AppController::reprocess(bool keepView)
{
    const auto* entry = (m_current >= 0 && m_current < m_entries.size()) ? &m_entries[m_current] : nullptr;
    if (!entry || !entry->data) return;
    if (m_future.isRunning()) {
        // Coalesce: run once more with the newest options when the current run ends.
        m_pending = true;
        m_pendingKeepView = m_pendingKeepView && keepView;
        return;
    }
    setBusy(true);
    m_future.setFuture(QtConcurrent::run(&AppController::runProcessing, entry->data, currentOptions(), ++m_generation,
                                         keepView));
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
