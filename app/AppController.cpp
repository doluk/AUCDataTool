// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppController.h"

#include "auc/Export.h"
#include "auc/Processing.h"
#include "auc/ScanTiming.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSet>
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

/// File system path for a dialog URL. On Android the native dialogs return content:// URIs;
/// documents on shared storage are mapped to their path (readable with "All files access"),
/// so that sibling files (*.mwrs.xml, other scans of a run) can be found. Other content URIs
/// are passed on unchanged – QFile opens them, but folder grouping is then not available.
QString toPath(const QUrl& url)
{
    if (url.isLocalFile()) return url.toLocalFile();
#ifdef Q_OS_ANDROID
    if (url.scheme() == QLatin1String("content")) {
        // .../document/<id>, .../tree/<id> or .../tree/<id>/document/<id>; the id is percent-encoded.
        const QStringList seg = url.path(QUrl::FullyEncoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString id;
        for (qsizetype i = 0; i + 1 < seg.size(); ++i)
            if (seg[i] == QLatin1String("document") || seg[i] == QLatin1String("tree"))
                id = QUrl::fromPercentEncoding(seg[i + 1].toUtf8());
        QString path;
        if (url.host() == QLatin1String("com.android.externalstorage.documents")) {
            const QString volume = id.section(QLatin1Char(':'), 0, 0);
            const QString rel = id.section(QLatin1Char(':'), 1);
            if (volume == QLatin1String("primary")) path = QStringLiteral("/storage/emulated/0/") + rel;
            else if (volume == QLatin1String("home")) path = QStringLiteral("/storage/emulated/0/Documents/") + rel;
            else if (!volume.isEmpty()) path = QStringLiteral("/storage/%1/").arg(volume) + rel;
        } else if (url.host() == QLatin1String("com.android.providers.downloads.documents")
                   && id.startsWith(QLatin1String("raw:"))) {
            path = id.mid(4);
        }
        if (!path.isEmpty() && QFileInfo(path).isReadable()) return QDir::cleanPath(path);
        return url.toString();
    }
#endif
    return url.toLocalFile();
}

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
    f.setDirectWriteFallback(true);  // e.g. Android content:// URIs: no temporary file next to the target
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
    // Common parent directory of all folders: groups are shown relative to it.
    QStringList root;
    bool first = true;
    for (const Entry& e : m_entries) {
        const QStringList parts = QDir::cleanPath(e.src->folder).split(QLatin1Char('/'));
        if (first) {
            root = parts;
            first = false;
            continue;
        }
        qsizetype n = 0;
        while (n < root.size() && n < parts.size() && root[n] == parts[n]) ++n;
        root = root.mid(0, n);
    }
    QVariantList list;
    for (const Entry& e : m_entries) {
        const auto& c = *e.src;
        QVariantMap m;
        m.insert(QStringLiteral("title"), channelTitle(c));
        m.insert(QStringLiteral("cell"), c.cell);
        m.insert(QStringLiteral("channel"), QString(QLatin1Char(c.channel)));
        const QStringList dirs = QDir::cleanPath(c.folder).split(QLatin1Char('/'));
        QString group = dirs.mid(root.size()).join(QLatin1Char('/'));
        if (group.isEmpty() && !dirs.isEmpty()) group = dirs.last();  // all in one folder
        m.insert(QStringLiteral("group"), group);
        m.insert(QStringLiteral("folder"), c.folder);
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

        // Separate fields for the channel list: optics (what was measured), source format,
        // wavelengths and scans.
        using auc::DataType;
        QString optics;
        switch (c.rawType) {
        case DataType::RadialAbsorbance: optics = tr("Absorbance"); break;
        case DataType::RadialIntensity: optics = tr("Intensity"); break;
        case DataType::Interference: optics = tr("Interference"); break;
        case DataType::Fluorescence: optics = tr("Fluorescence"); break;
        case DataType::WavelengthAbsorbance: optics = tr("Absorbance spectra"); break;
        case DataType::WavelengthIntensity: optics = tr("Intensity spectra"); break;
        }
        m.insert(QStringLiteral("optics"), optics);
        QString source;
        switch (c.format) {
        case auc::SourceFormat::Xl: source = tr("Beckman XL"); break;
        case auc::SourceFormat::Auc: source = tr("openAUC"); break;
        case auc::SourceFormat::Mwrs: source = tr("MWL .mwrs"); break;
        case auc::SourceFormat::Mw: source = tr("MWL .mw"); break;
        }
        m.insert(QStringLiteral("source"), source);
        m.insert(QStringLiteral("format"), c.formatName);
        m.insert(QStringLiteral("isReference"), c.formatName.endsWith(QLatin1String(" reference")));
        m.insert(QStringLiteral("wavelengthText"),
                 c.xIsWavelength ? tr("%1–%2 nm").arg(c.radius.empty() ? 0.0 : c.radius.front(), 0, 'f', 0).arg(c.radius.empty() ? 0.0 : c.radius.back(), 0, 'f', 0)
                 : c.wavelengths.size() > 1
                     ? tr("%1 λ, %2–%3 nm").arg(c.wavelengths.size()).arg(c.wavelengths.front(), 0, 'f', 0).arg(c.wavelengths.back(), 0, 'f', 0)
                 : !c.wavelengths.empty() ? tr("%1 nm").arg(c.wavelengths.front(), 0, 'f', 0)
                                          : QString());
        m.insert(QStringLiteral("scanText"), tr("%n scan(s)", nullptr, int(c.scans.size())));
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
    for (const QUrl& u : urls) paths << toPath(u);
    openPaths(paths);
}

void AppController::openFolder(const QUrl& url, bool watchLive)
{
    const QString dir = toPath(url);
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
        updateRunPlots();
        emit datasetChanged();
        emit viewChanged();
        reprocess(true);  // keep the user's zoom during a run
    }
    updateScanTiming();  // other cells of the run may have new scans
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
    updateRunPlots();
    updateScanTiming();
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
    updateRunPlots();
    updateScanTiming();

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
bool AppController::isInterference() const { return currentSrc() && currentSrc()->rawType == auc::DataType::Interference; }
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
    const auto mode = m == 2 ? ReferenceMode::RadialArea : m == 1 ? ReferenceMode::MeanOfScans : ReferenceMode::ScanByScan;
    if (mode == e->view.refMode) return;
    e->view.refMode = mode;
    if (mode == ReferenceMode::RadialArea && e->view.refR1 == e->view.refR2 && e->src->radius.size() > 1) {
        // Initial region: the innermost 5 % of the radius range.
        const double a = e->src->radius.front(), b = e->src->radius.back();
        e->view.refR1 = a;
        e->view.refR2 = a + 0.05 * (b - a);
    }
    if (mode == ReferenceMode::RadialArea && e->view.display == DisplayMode::Intensity && !e->src->absorbanceData
        && e->src->valueLabel.isEmpty())
        e->view.display = DisplayMode::Absorbance;  // the region alone defines I0
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
double AppController::refR1() const { return current() ? current()->view.refR1 : 0.0; }
void AppController::setRefR1(double r)
{
    Entry* e = current();
    if (!e || r == e->view.refR1) return;
    e->view.refR1 = r;
    emit viewChanged();
    reprocess(true);
}
double AppController::refR2() const { return current() ? current()->view.refR2 : 0.0; }
void AppController::setRefR2(double r)
{
    Entry* e = current();
    if (!e || r == e->view.refR2) return;
    e->view.refR2 = r;
    emit viewChanged();
    reprocess(true);
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
    o.fringeJumps = m_optFringe;
    o.fringeAlign = m_optFringeAlign;
    o.fringeThreshold = std::max(0.05, m_optFringeThreshold);
    o.fringeR1 = m_optFringeR1;
    o.fringeR2 = m_optFringeR2;
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
    res.spectrumDeferred = job.spectrumDeferred;
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
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()
        && v.refMode == ReferenceMode::RadialArea && !job.ref) {
        // I0 from a region of this channel itself.
        const auc::Dataset self = *d;
        if (std::string err = auc::proc::absorbanceRadialReference(*d, self, v.refR1, v.refR2); !err.empty()) {
            res.error = tr("Reference: %1").arg(QString::fromStdString(err));
            return res;
        }
    } else if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()) {
        if (!job.ref) {
            res.error = tr("Choose a reference channel or the radial-area reference to show absorbance.");
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
        } else if (v.refMode == ReferenceMode::RadialArea) {
            const std::size_t before = d->scans.size();
            err = auc::proc::absorbanceRadialReference(*d, rd, v.refR1, v.refR2);
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
    if (o.fringeJumps && src.rawType == auc::DataType::Interference && d->scans.size() >= 2) {
        // Fringe-jump correction (S. Xu et al., AUC 2026): the alignment suggestions come
        // from the jitter-corrected scans before the jumps are removed.
        auc::proc::FringeJumpParams fp{o.fringeThreshold, o.fringeR1, o.fringeR2};
        const bool autoLimits = !(fp.r2 > fp.r1);
        if (autoLimits && !d->radius.empty()) {
            // No meniscus/bottom given: the solution column from the estimated meniscus (where
            // the scans start to differ) to the last point. An air–air baseline region in the
            // first half lies above the meniscus: the column starts after it at the earliest
            // (inside it every jitter-corrected scan is 0 and would look perfectly aligned).
            const double lo = std::min(d->radius.front(), d->radius.back());
            const double hi = std::max(d->radius.front(), d->radius.back());
            double r1 = auc::proc::estimateMeniscus(*d);
            if (!std::isfinite(r1) || r1 > lo + 0.8 * (hi - lo)) r1 = lo + 0.1 * (hi - lo);
            if (o.offsetMode == ProcessingOptions::OffsetRegion) {
                const double air = std::max(o.offsetR1, o.offsetR2);
                if (air > r1 && air < lo + 0.5 * (hi - lo)) r1 = air;
            }
            fp.r1 = r1;
            // Bottom: where the scans turn into noise, else the last point.
            const double bottom = auc::proc::estimateCellBottom(*d, r1);
            fp.r2 = std::isfinite(bottom) && bottom > r1 ? bottom : hi;
        }
        const auc::proc::FringeAlignment a = auc::proc::analyzeFringeAlignment(*d, fp);
        const double align = o.fringeAlign > 0.0 ? o.fringeAlign : a.suggested;
        QVariantMap f;
        f.insert(QStringLiteral("r1"), fp.r1);
        f.insert(QStringLiteral("r2"), fp.r2);
        f.insert(QStringLiteral("limitsAuto"), autoLimits);
        f.insert(QStringLiteral("centre"), a.centre);
        f.insert(QStringLiteral("stableValid"), a.stableValid);
        f.insert(QStringLiteral("stableLo"), a.stableLo);
        f.insert(QStringLiteral("stableHi"), a.stableHi);
        f.insert(QStringLiteral("stableAlign"), a.stableAlign);
        f.insert(QStringLiteral("selfConsistentValid"), a.selfConsistentValid);
        f.insert(QStringLiteral("selfConsistent"), a.selfConsistent);
        if (align > 0.0) {
            f.insert(QStringLiteral("jumps"), qulonglong(auc::proc::removeFringeJumps(*d, align, fp)));
            const auto& first = d->scans.front().values;
            const double iso = auc::proc::crossingRadius(d->radius, first, d->scans.back().values, align, fp.r1, fp.r2);
            f.insert(QStringLiteral("valid"), true);
            f.insert(QStringLiteral("alignment"), align);
            if (std::isfinite(iso)) {
                const double pos = auc::proc::thresholdIndex(d->radius, iso);
                const std::size_t i = std::min<std::size_t>(std::size_t(pos), d->pointCount() - 2);
                f.insert(QStringLiteral("iso"), iso);
                f.insert(QStringLiteral("isoFringes"), first[i] + (pos - double(i)) * (first[i + 1] - first[i]));
            }
        } else {
            f.insert(QStringLiteral("valid"), false);
            warnings << tr("fringe jumps: the first and last scan do not cross – set the alignment radius");
        }
        res.fringe = f;
    }

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
        const bool radial = v.refMode == ReferenceMode::RadialArea;
        const auc::ChannelSource* ref = radial && !job.ref ? &src : job.ref.get();
        if (!ref || ref->absorbanceData) return;  // reported by the main pipeline
        std::size_t rf = 0, rc = 1;
        if (radial) {
            // Spectra averaged over the reference region give I0 per scan and wavelength.
            const std::size_t a = auc::proc::nearestIndex(ref->radius, v.refR1);
            const std::size_t b = auc::proc::nearestIndex(ref->radius, v.refR2);
            rf = std::min(a, b);
            rc = std::max(a, b) - rf + 1;
        } else {
            window(*ref, rf, rc);
        }
        std::shared_ptr<const auc::Dataset> rraw;
        if (auc::IoResult r = ref->spectra(rf, rc, rraw); !r.ok()) {
            warnings << tr("reference spectrum: %1").arg(r.message);
            return;
        }
        auc::Dataset rd = *rraw;
        adjustDark(rd, *ref);
        if (rd.radius != d.radius) {
            warnings << tr("spectrum: the reference channel has other wavelengths");
            return;
        }
        const std::string err = v.refMode == ReferenceMode::MeanOfScans
            ? auc::proc::absorbanceMeanReference(d, rd, std::size_t(std::max(0, v.refFirst)),
                                                 v.refLast < 0 ? SIZE_MAX : std::size_t(v.refLast))
            : auc::proc::absorbanceScanByScan(d, rd);
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
        for (std::size_t r : rows)
            g->z.push_back(float(job.surfaceOmega2t ? processed.scans[r].omega2t : processed.scans[r].seconds / 60.0));
        g->zTitle = job.surfaceOmega2t ? tr("ω²t (rad²/s)") : tr("Time (min)");
        // Rows must be monotonic in z (reverse/selection keep the order; ω²t could repeat).
        for (std::size_t i = 1; i < g->z.size(); ++i)
            if (!(g->z[i] > g->z[i - 1])) g->z[i] = std::nextafter(g->z[i - 1], std::numeric_limits<float>::max());
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
        const std::size_t n = c.radius.size();
        for (std::size_t k = 0; k < nwl && k < c.darkCurrent.size(); ++k)
            for (std::size_t j = 0; j < n; ++j) mat[k * n + j] += sign * c.darkCurrent[k];
    };
    adjustDark(m, src);
    if (v.display == DisplayMode::Absorbance && !src.absorbanceData && src.valueLabel.isEmpty()) {
        const bool radial = v.refMode == ReferenceMode::RadialArea;
        const auc::ChannelSource* ref = radial && !job.ref ? &src : job.ref.get();
        if (!ref || ref->absorbanceData) {
            error = tr("Choose a reference channel or the radial-area reference to show absorbance.");
            return nullptr;
        }
        const std::size_t rnp = ref->radius.size();
        if (ref->wavelengths.size() != nwl || (!radial && rnp != np)) {
            error = tr("The reference channel has another wavelength/radius grid.");
            return nullptr;
        }
        std::vector<float> rm;
        if (radial) {
            if (scan >= ref->scans.size()) {
                error = tr("The reference has only %1 scans.").arg(ref->scans.size());
                return nullptr;
            }
            std::vector<float> one;
            if (ref == &src) {
                one = m;  // already dark-adjusted
            } else {
                if (auc::IoResult r = ref->scanMatrix(scan, one); !r.ok()) {
                    error = r.message;
                    return nullptr;
                }
                adjustDark(one, *ref);
            }
            // I0 per wavelength: mean over the region, broadcast over the sample's radius grid.
            rm.resize(nwl * np);
            for (std::size_t k = 0; k < nwl; ++k) {
                const double i0 = auc::proc::windowMean(std::span<const float>(one).subspan(k * rnp, rnp), ref->radius,
                                                        v.refR1, v.refR2);
                if (std::isnan(i0)) {
                    error = tr("Reference: no radius points in the reference region");
                    return nullptr;
                }
                std::fill_n(rm.begin() + std::ptrdiff_t(k * np), np, float(i0));
            }
        } else if (v.refMode == ReferenceMode::ScanByScan) {
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
        if (!radial) adjustDark(rm, *ref);
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
    // The spectra plot is hidden behind the 3D tab; reading every scan for it there only
    // delays the surface. setSurfaceActive(false) reprocesses to fill it again.
    job.spectrumDeferred = job.opt.spectrum && m_spectrumPlot && m_surfaceActive;
    job.opt.spectrum = job.opt.spectrum && m_spectrumPlot && !m_surfaceActive;
    job.ti = e.ti;
    job.ri = e.ri;
    job.surface = m_surfaceActive;
    job.surfaceMode = hasSpectra() ? m_surfaceMode : SurfaceMode::RadiusTime;
    job.surfaceOmega2t = m_surfaceOmega2t;
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
    if (r.fringe != m_fringeInfo) {
        m_fringeInfo = r.fringe;
        emit fringeInfoChanged();
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
    if (m_spectrumPlot && !r.spectrumDeferred) {
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

void AppController::setSpeedPlot(ScanPlot* p)
{
    if (m_speedPlot == p) return;
    m_speedPlot = p;
    emit runPlotsChanged();
    updateRunPlots();
}

void AppController::setTemperaturePlot(ScanPlot* p)
{
    if (m_temperaturePlot == p) return;
    m_temperaturePlot = p;
    emit runPlotsChanged();
    updateRunPlots();
}

void AppController::setOmega2tPlot(ScanPlot* p)
{
    if (m_omega2tPlot == p) return;
    m_omega2tPlot = p;
    emit runPlotsChanged();
    updateRunPlots();
}

void AppController::updateRunPlots()
{
    // Rotor speed, temperature and ω²t of every scan against time, from the scan headers
    // (no readings needed, so done on the GUI thread).
    const auc::ChannelSource* c = currentSrc();
    auto set = [](ScanPlot* p, PlotSeriesPtr series, const QHash<int, CurveStyle>& styles) {
        if (!p) return;
        p->setCurveStyles(styles);
        p->setSeries(std::move(series), false);
    };
    if (!c || c->scans.empty()) {
        set(m_speedPlot, nullptr, {});
        set(m_temperaturePlot, nullptr, {});
        set(m_omega2tPlot, nullptr, {});
        if (!m_runConditions.isEmpty() || m_runSweepLog) {
            m_runConditions.clear();
            m_runSweepLog = false;
            emit runConditionsChanged();
        }
        return;
    }
    const auto& sc = c->scans;
    const std::size_t n = sc.size();
    std::vector<float> minutes(n), measured(n), setSpeed(n), temp(n), w2tf(n), line(n);
    std::vector<double> seconds(n), w2t(n), rpm(n);
    bool hasSet = false;
    for (std::size_t i = 0; i < n; ++i) {
        minutes[i] = float(sc[i].seconds / 60.0);
        seconds[i] = sc[i].seconds;
        w2t[i] = sc[i].omega2t;
        rpm[i] = sc[i].rpm;
        measured[i] = float(sc[i].rpm);
        setSpeed[i] = float(sc[i].setRpm);
        hasSet = hasSet || sc[i].setRpm > 0;
        temp[i] = float(sc[i].temperature);
        w2tf[i] = float(sc[i].omega2t);
    }
    using Curves = std::vector<std::pair<std::vector<float>, QColor>>;
    auto series = [&](Curves curves, const QStringList& labels) {
        auto s = std::make_shared<PlotSeries>();
        s->x = minutes;
        for (std::size_t k = 0; k < curves.size(); ++k) {
            s->y.push_back(std::move(curves[k].first));
            s->colors.push_back(curves[k].second);
            s->ids.push_back(int(k));
            s->labels.push_back(labels.value(int(k)));
        }
        s->computeBounds();
        return PlotSeriesPtr(s);
    };
    CurveStyle points;
    points.marker = CurveStyle::Circle;
    points.markerSize = 5.0f;
    CurveStyle dashed;
    dashed.line = CurveStyle::Dash;
    dashed.width = 1.5f;

    // Speed: measured, and the set speed where the format stores it.
    Curves speedCurves{{measured, QColor(0x1d, 0x4e, 0xd8)}};
    if (hasSet) speedCurves.push_back({setSpeed, QColor(0x9c, 0xa3, 0xaf)});
    set(m_speedPlot, series(std::move(speedCurves), {tr("measured"), tr("set")}), {{0, points}, {1, dashed}});
    set(m_temperaturePlot, series({{temp, QColor(0xdc, 0x26, 0x26)}}, {tr("temperature")}), {{0, points}});

    // ω²t with the straight line ω²·(t − t₀) through the scans at constant speed.
    const auc::proc::Omega2tFit fit = auc::proc::fitOmega2t(seconds, w2t, rpm);
    for (std::size_t i = 0; i < n; ++i) line[i] = float(fit.slope * seconds[i] + fit.intercept);
    // Sweep logs (.tr): ω²t at every radius point as a thin line per sweep, on a common
    // time axis with the scans (scan times precede their sweep, so each sweep stays one
    // unbroken stretch between them).
    std::size_t logSweeps = 0;
    {
        const auto traces = c->scanTraces();
        struct Pt {
            double t, v;
            bool log;
        };
        std::vector<Pt> pts;
        for (std::size_t i = 0; i < n; ++i) pts.push_back({seconds[i], w2t[i], false});
        for (const auto& t : traces) {
            bool any = false;
            for (std::size_t j = 0; j < t.seconds.size(); ++j)
                if (std::isfinite(t.seconds[j]) && std::isfinite(t.omega2t[j])) {
                    pts.push_back({t.seconds[j], t.omega2t[j], true});
                    any = true;
                }
            logSweeps += any ? 1 : 0;
        }
        if (logSweeps == 0) {
            Curves w2tCurves{{w2tf, QColor(0x05, 0x96, 0x69)}};
            if (fit.valid) w2tCurves.push_back({line, QColor(0x6b, 0x72, 0x80)});
            set(m_omega2tPlot, series(std::move(w2tCurves), {tr("ω²t"), tr("fit")}), {{0, points}, {1, dashed}});
        } else {
            std::stable_sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) { return a.t < b.t; });
            constexpr float fnan = std::numeric_limits<float>::quiet_NaN();
            auto s = std::make_shared<PlotSeries>();
            std::vector<float> head(pts.size(), fnan), fitted(pts.size(), fnan), logged(pts.size(), fnan);
            for (std::size_t i = 0; i < pts.size(); ++i) {
                s->x.push_back(float(pts[i].t / 60.0));
                (pts[i].log ? logged : head)[i] = float(pts[i].v);
                if (fit.valid) fitted[i] = float(fit.slope * pts[i].t + fit.intercept);
            }
            auto add = [&](std::vector<float> y, QColor col, int id, const QString& label) {
                s->y.push_back(std::move(y));
                s->colors.push_back(col);
                s->ids.push_back(id);
                s->labels.push_back(label);
            };
            add(std::move(logged), QColor(0x86, 0xd1, 0xb4), 2, tr("ω²t logged per radius (.tr)"));
            add(std::move(head), QColor(0x05, 0x96, 0x69), 0, tr("ω²t"));
            if (fit.valid) add(std::move(fitted), QColor(0x6b, 0x72, 0x80), 1, tr("fit"));
            s->computeBounds();
            CurveStyle logLine;
            logLine.width = 1.5f;
            set(m_omega2tPlot, s, {{0, points}, {1, dashed}, {2, logLine}});
        }
    }

    const auto [rmin, rmax] = std::minmax_element(rpm.begin(), rpm.end());
    const auto [tmin, tmax] = std::minmax_element(temp.begin(), temp.end());
    QStringList parts;
    parts << tr("%n scan(s), %1–%2 min", nullptr, int(n)).arg(minutes.front(), 0, 'f', 1).arg(minutes.back(), 0, 'f', 1);
    parts << tr("speed %1–%2 rpm").arg(*rmin, 0, 'f', 0).arg(*rmax, 0, 'f', 0);
    parts << tr("temperature %1–%2 °C").arg(*tmin, 0, 'f', 1).arg(*tmax, 0, 'f', 1);
    if (fit.valid)
        parts << tr("ω²t fit over %1 scans: %2 rpm effective, t₀ = %3 s, rms %4 %")
                     .arg(fit.used)
                     .arg(fit.rpm, 0, 'f', 0)
                     .arg(fit.t0, 0, 'f', 1)
                     .arg(fit.rms * 100.0, 0, 'g', 2);
    if (logSweeps) parts << tr("ω²t logged per radius point in %n sweep(s) (.tr)", nullptr, int(logSweeps));
    const QString text = parts.join(QStringLiteral(" · "));
    if (text != m_runConditions || m_runSweepLog != (logSweeps > 0)) {
        m_runConditions = text;
        m_runSweepLog = logSweeps > 0;
        emit runConditionsChanged();
    }
}

// ---------------------------------------------------------------------------------------
// Scan timing (XL and openAUC radial absorbance/intensity)
// ---------------------------------------------------------------------------------------

namespace {

/// Absorbance, pseudo-absorbance or intensity against radius, any file format (not
/// interference or fluorescence, not wavelength scans).
bool timingCapable(const auc::ChannelSource& c)
{
    return c.valueLabel.isEmpty() && !c.xIsWavelength
        && (c.rawType == auc::DataType::RadialAbsorbance || c.rawType == auc::DataType::RadialIntensity);
}

QColor timingColor(std::size_t k)
{
    static const QColor colors[] = {QColor(0xe6, 0x9f, 0x00), QColor(0xd9, 0x70, 0x9f), QColor(0x4b, 0x3a, 0xa6),
                                    QColor(0x2b, 0x7b, 0xd6), QColor(0x1a, 0xaa, 0x7a), QColor(0x12, 0x85, 0x0f),
                                    QColor(0xe8, 0x61, 0x3c), QColor(0x7a, 0x7a, 0x7a)};
    return colors[k % std::size(colors)];
}

/// Deterministic jitter in [−0.5, 0.5) for the strip plot.
float jitter(std::size_t i)
{
    std::uint32_t h = std::uint32_t(i) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h & 0xffff) / 65536.0f - 0.5f;
}

}  // namespace

bool AppController::hasScanTiming() const
{
    const auto* c = currentSrc();
    return c && timingCapable(*c);
}

void AppController::setTimingPlot(ScanPlot* p)
{
    if (m_timingPlot == p) return;
    m_timingPlot = p;
    emit timingPlotsChanged();
    updateScanTiming(true);
}

void AppController::setTransitionPlot(ScanPlot* p)
{
    if (m_transitionPlot == p) return;
    m_transitionPlot = p;
    emit timingPlotsChanged();
    updateScanTiming(true);
}

void AppController::setSweepPlot(ScanPlot* p)
{
    if (m_sweepPlot == p) return;
    m_sweepPlot = p;
    emit timingPlotsChanged();
    updateScanTiming(true);
}

void AppController::setSweepSpeedPlot(ScanPlot* p)
{
    if (m_sweepSpeedPlot == p) return;
    m_sweepSpeedPlot = p;
    emit timingPlotsChanged();
    updateScanTiming(true);
}

void AppController::setTimingCellPlot(int index, ScanPlot* plot)
{
    m_timingCellPlots[index] = plot;
    if (!plot) return;
    CurveStyle s;
    s.marker = CurveStyle::Circle;
    s.markerSize = 3.5f;
    plot->setCurveStyles({{0, s}});
    plot->setSeries(index >= 0 && std::size_t(index) < m_timing.cellSeries.size() ? m_timing.cellSeries[std::size_t(index)] : nullptr,
                    false);
}

void AppController::updateScanTiming(bool force)
{
    using namespace auc::timing;
    const auc::ChannelSource* cur = currentSrc();

    // The run: the absorbance/intensity channels of the current channel's folder.
    std::vector<const auc::ChannelSource*> run;
    qsizetype files = 0;
    if (cur && timingCapable(*cur))
        for (const Entry& e : m_entries)
            if (timingCapable(*e.src) && e.src->folder == cur->folder) {
                run.push_back(e.src.get());
                files += e.src->files.size();
            }
    const QString key = run.empty() ? QString() : QStringLiteral("%1|%2|%3").arg(cur->folder).arg(run.size()).arg(files);
    if (!force && key == m_timing.key) return;
    // Keep the zoom when the same run only grew (live mode).
    const bool keep = !force && !key.isEmpty() && m_timing.key.section(QLatin1Char('|'), 0, 0) == cur->folder;
    m_timing = Timing{};
    m_timing.key = key;

    Analysis a;
    if (!run.empty()) {
        std::vector<Slot> cellSlots;
        for (const auto* c : run) {
            // One list per wavelength (scanned separately), or one for all of them
            // (multi-wavelength detector: all wavelengths recorded at once).
            const auto per = c->wavelengthScans();
            const bool atOnce = per.size() == 1 && c->wavelengths.size() > 1;
            for (std::size_t w = 0; w < per.size() && w < c->wavelengths.size(); ++w) {
                Slot s{c->cell, c->wavelengths[w], {}, c->channel, atOnce ? c->wavelengths.back() : c->wavelengths[w], {}};
                for (const auc::ScanInfo& si : per[w]) s.scans.push_back({si.seconds, si.number});
                cellSlots.push_back(std::move(s));
            }
        }
        a = analyze(std::move(cellSlots));
    }

    if (!a.events.empty()) {
        const std::size_t ns = a.cellSlots.size();
        // Slot names show the channel / wavelength only where a cell has several of them.
        bool multiWl = false, multiCh = false;
        for (std::size_t k = 0; k < ns; ++k)
            for (std::size_t l = 0; l < ns; ++l) {
                const Slot &p = a.cellSlots[k], &q = a.cellSlots[l];
                if (l == k || p.cell != q.cell) continue;
                if (std::abs(p.wavelength - q.wavelength) >= 0.05) multiWl = true;
                else if (p.channel != q.channel) multiCh = true;
            }
        // Channels recorded in the same scans (one slot): "1 A+B".
        bool merged = false;
        for (const Slot& s : a.cellSlots) merged |= s.channels.size() > 1;
        auto cellName = [&](std::size_t k) {
            const Slot& s = a.cellSlots[k];
            if (s.channels.size() > 1) {
                QStringList chs;
                for (char c : s.channels) chs << QString(QLatin1Char(c));
                return QStringLiteral("%1 %2").arg(s.cell).arg(chs.join(QLatin1Char('+')));
            }
            return multiCh || merged ? QStringLiteral("%1%2").arg(s.cell).arg(QLatin1Char(s.channel)) : QString::number(s.cell);
        };
        auto wavelengthText = [&](const Slot& s) {
            return s.wavelengthTo > s.wavelength + 0.05
                       ? tr("%1–%2 nm at once").arg(s.wavelength, 0, 'f', 0).arg(s.wavelengthTo, 0, 'f', 0)
                       : tr("%1 nm").arg(s.wavelength, 0, 'f', 0);
        };
        // Colours by cell (in acquisition order): with several wavelengths per cell, one colour
        // per cell/wavelength would repeat the palette and hide which cell a point belongs to.
        QStringList cellOrder;  // cell names ("4", or "1A" with several channels per cell)
        for (std::size_t k = 0; k < ns; ++k)
            if (!cellOrder.contains(cellName(k))) cellOrder << cellName(k);
        auto color = [&](std::size_t k) { return timingColor(std::size_t(cellOrder.indexOf(cellName(k)))); };
        auto slotName = [&](std::size_t k) {
            const Slot& s = a.cellSlots[k];
            return multiWl ? QStringLiteral("%1/%2").arg(cellName(k)).arg(s.wavelength, 0, 'f', 0) : cellName(k);
        };
        const double span = a.events.back().seconds;
        const bool hours = span > 3 * 3600.0;
        const double unit = hours ? 3600.0 : 60.0;
        m_timing.timeLabel = hours ? tr("Elapsed time at scan (h)") : tr("Elapsed time at scan (min)");
        auto fmt = [](double s) { return QString::number(s, 'f', std::abs(s - std::round(s)) < 0.05 ? 0 : 1); };

        // Interleaved: interval to the next scan, one curve per cell just scanned.
        {
            auto s = std::make_shared<PlotSeries>();
            s->x.reserve(a.events.size());
            for (const Event& e : a.events) s->x.push_back(float(e.seconds / unit));
            for (std::size_t k = 0; k < ns; ++k) {
                std::vector<float> y(a.events.size(), std::numeric_limits<float>::quiet_NaN());
                for (std::size_t i = 0; i < a.events.size(); ++i)
                    if (std::size_t(a.events[i].slot) == k && std::isfinite(a.next[i])) y[i] = float(a.next[i]);
                const QString label = tr("after cell %1").arg(slotName(k));
                s->y.push_back(std::move(y));
                s->colors.push_back(color(k));
                s->ids.push_back(int(k));
                s->labels.push_back(label);
            }
            for (qsizetype c = 0; c < cellOrder.size(); ++c)
                m_timing.legend.append(QVariantMap{{QStringLiteral("label"), tr("after cell %1").arg(cellOrder[c])},
                                                   {QStringLiteral("color"), timingColor(std::size_t(c))}});
            s->computeBounds();
            m_timing.interleaved = s;
        }

        // Transitions: one row each (y = −row, jittered), intervals on the shared, sorted x.
        {
            struct Pt {
                float x;
                std::size_t row;
                float y;
            };
            std::vector<Pt> pts;
            for (std::size_t r = 0; r < a.transitions.size(); ++r) {
                const Transition& t = a.transitions[r];
                for (std::size_t i = 0; i < t.intervals.size(); ++i)
                    pts.push_back({float(t.intervals[i]), r, -float(r) + 0.6f * jitter(pts.size())});
                QString label = QStringLiteral("%1→%2").arg(slotName(std::size_t(t.from)), slotName(std::size_t(t.to)));
                if (!t.skipped.empty()) {
                    QStringList sk;
                    for (int k : t.skipped) sk << slotName(std::size_t(k));
                    label += tr(" (%1 skipped)").arg(sk.join(QStringLiteral(", ")));
                } else if (!t.regular) {
                    label += tr(" (irregular)");
                }
                m_timing.transitionTicks.append(QVariantMap{{QStringLiteral("value"), -double(r)}, {QStringLiteral("label"), label}});
                m_timing.transitionNotes.append(QVariantMap{{QStringLiteral("y"), -double(r)},
                                                            {QStringLiteral("right"), true},
                                                            {QStringLiteral("text"), tr("med %1 s  n=%2").arg(fmt(t.median)).arg(t.intervals.size())}});
            }
            std::sort(pts.begin(), pts.end(), [](const Pt& p, const Pt& q) { return p.x < q.x; });
            auto s = std::make_shared<PlotSeries>();
            for (const Pt& p : pts) s->x.push_back(p.x);
            for (std::size_t r = 0; r < a.transitions.size(); ++r) {
                std::vector<float> y(pts.size(), std::numeric_limits<float>::quiet_NaN());
                for (std::size_t i = 0; i < pts.size(); ++i)
                    if (pts[i].row == r) y[i] = pts[i].y;
                s->y.push_back(std::move(y));
                s->colors.push_back(color(std::size_t(a.transitions[r].from)));
                s->ids.push_back(int(r));
                s->labels.push_back(m_timing.transitionTicks[qsizetype(r)].toMap().value(QStringLiteral("label")).toString());
            }
            s->computeBounds();
            m_timing.transitions = s;
        }

        // Skips: fixed markers at the scan before the gap.
        QMap<int, int> skippedCount;
        for (const Skip& sk : a.skips) {
            for (int k : sk.skipped) ++skippedCount[k];
            m_timing.skipMarkers.append(QVariantMap{{QStringLiteral("value"), sk.seconds / unit},
                                                    {QStringLiteral("color"), QColor(0xdc, 0x26, 0x26)},
                                                    {QStringLiteral("label"), QString()},
                                                    {QStringLiteral("key"), QString()},
                                                    {QStringLiteral("fixed"), true}});
        }

        // Per cell: interval between consecutive scans of the same cell; outliers labelled.
        for (std::size_t k = 0; k < ns; ++k) {
            const Slot& sl = a.cellSlots[k];
            const double med = a.slotMedian[k];
            auto s = std::make_shared<PlotSeries>();
            std::vector<float> y;
            struct Peak {
                double dt;
                QVariantMap note;
            };
            std::vector<Peak> peaks;
            for (std::size_t j = 0; j + 1 < sl.scans.size(); ++j) {
                const double dt = sl.scans[j + 1].seconds - sl.scans[j].seconds;
                s->x.push_back(float(sl.scans[j].seconds / unit));
                y.push_back(float(dt));
                if (std::isfinite(med) && dt > 1.25 * med && dt - med > 5.0) {
                    QString text = fmt(dt);
                    const int n0 = sl.scans[j].number, n1 = sl.scans[j + 1].number;
                    if (n1 - n0 > 1) text += QStringLiteral(" (s%1→%2)").arg(n0).arg(n1);
                    peaks.push_back({dt, QVariantMap{{QStringLiteral("x"), sl.scans[j].seconds / unit},
                                                     {QStringLiteral("y"), dt},
                                                     {QStringLiteral("text"), text}}});
                }
            }
            // At most 40 labels per cell, the largest.
            std::sort(peaks.begin(), peaks.end(), [](const Peak& p, const Peak& q) { return p.dt > q.dt; });
            QVariantList notes;
            for (std::size_t i = 0; i < peaks.size() && i < 40; ++i) notes.append(peaks[i].note);
            if (!y.empty()) {
                s->y.push_back(std::move(y));
                s->colors.push_back(color(k));
                s->ids.push_back(0);
                s->labels.push_back(tr("cell %1").arg(slotName(k)));
                s->computeBounds();
                m_timing.cellSeries.push_back(s);
            } else {
                m_timing.cellSeries.push_back(nullptr);
            }
            m_timing.cells.append(QVariantMap{
                {QStringLiteral("title"), tr("Cell %1").arg(cellName(k))},
                {QStringLiteral("info"), tr("%1 · median %2 s · n = %3")
                                             .arg(wavelengthText(sl))
                                             .arg(std::isfinite(med) ? fmt(med) : QStringLiteral("–"))
                                             .arg(sl.scans.empty() ? 0 : sl.scans.size() - 1)},
                {QStringLiteral("color"), color(k)},
                {QStringLiteral("notes"), notes}});
        }

        // Summary
        QStringList order;
        for (std::size_t k = 0; k < ns; ++k) order << slotName(k);
        QStringList parts;
        QSet<int> cells;
        for (const Slot& s : a.cellSlots) cells.insert(s.cell);
        parts << tr("%1 · %n cell(s)", nullptr, int(cells.size())).arg(cur->runId);
        parts << tr("%1 scans over %2 %3").arg(a.events.size()).arg(span / unit, 0, 'f', 1).arg(hours ? tr("h") : tr("min"));
        parts << tr("order %1").arg(order.join(QStringLiteral("→")));
        std::vector<double> cycles;
        for (double m : a.slotMedian)
            if (std::isfinite(m)) cycles.push_back(m);
        if (!cycles.empty()) parts << tr("median cycle %1 s").arg(fmt(median(cycles)));
        if (a.skips.empty()) {
            parts << tr("no skipped scans");
        } else {
            QStringList sk;
            for (auto it = skippedCount.cbegin(); it != skippedCount.cend(); ++it)
                sk << tr("cell %1 ×%2").arg(slotName(std::size_t(it.key()))).arg(it.value());
            parts << tr("skipped: %1 (red dashed lines)").arg(sk.join(QStringLiteral(", ")));
        }
        if (merged) parts << tr("A+B: channels recorded in the same scans (identical times)");
        if (a.integerSeconds) parts << tr("times are whole seconds (±0.5 s per scan)");
        m_timing.summary = parts.join(QStringLiteral(" · "));

        // Sweep logs (.tr, multi-wavelength detector): from the current channel, else the
        // first channel of the run that has them. One sweep covers all channels read in it.
        std::vector<auc::ScanTrace> traces;
        const auc::ChannelSource* traced = nullptr;
        for (const auc::ChannelSource* c : run) {
            if (c != cur && traced) continue;
            auto t = c->scanTraces();
            if (t.empty()) continue;
            traces = std::move(t);
            traced = c;
            if (c == cur) break;
        }
        if (traced) {
            constexpr float fnan = std::numeric_limits<float>::quiet_NaN();
            const std::size_t np = traced->radius.size();
            std::size_t nt = 0;
            for (const auto& t : traces) nt += t.seconds.empty() ? 0 : 1;
            // Time since the sweep start against radius, one curve per scan.
            auto sw = std::make_shared<PlotSeries>();
            for (double r : traced->radius) sw->x.push_back(float(r));
            // Rotor speed against run time: all logs in a row, a gap between sweeps.
            auto sp = std::make_shared<PlotSeries>();
            std::vector<float> rpm;
            std::vector<double> durations, pauses;
            double prevEnd = std::numeric_limits<double>::quiet_NaN(), rpmLo = 1e300, rpmHi = -1e300;
            std::size_t k = 0;
            for (const auto& t : traces) {
                if (t.seconds.empty()) {
                    prevEnd = std::numeric_limits<double>::quiet_NaN();
                    continue;
                }
                const double t0 = t.start(), t1 = t.end();
                std::vector<float> y(np, fnan);
                for (std::size_t j = 0; j < np && j < t.seconds.size(); ++j) y[j] = float(t.seconds[j] - t0);
                sw->y.push_back(std::move(y));
                sw->colors.push_back(Colormap::color(Colormap::Kind(std::clamp(m_optColormap, 0, 3)), nt > 1 ? double(k) / double(nt - 1) : 0.0));
                sw->ids.push_back(int(k));
                sw->labels.push_back(tr("scan %1").arg(t.number));
                ++k;
                durations.push_back(t1 - t0);
                if (std::isfinite(prevEnd)) pauses.push_back(t0 - prevEnd);
                prevEnd = t1;
                if (!sp->x.empty()) {  // gap
                    sp->x.push_back(float(t0 / unit));
                    rpm.push_back(fnan);
                }
                for (std::size_t j = 0; j < t.seconds.size(); ++j) {
                    if (!std::isfinite(t.seconds[j]) || !std::isfinite(t.rpm[j])) continue;  // dropout
                    sp->x.push_back(float(t.seconds[j] / unit));
                    rpm.push_back(float(t.rpm[j]));
                    rpmLo = std::min(rpmLo, t.rpm[j]);
                    rpmHi = std::max(rpmHi, t.rpm[j]);
                }
            }
            // Straight line per sweep: time against radius. Its slope gives the time per
            // radial position; R² and the rms residual how steadily the detector moves.
            std::vector<double> slopes, r2s, offsets;
            double ss = 0;
            std::size_t nres = 0;
            const std::span<const double> rad(traced->radius);
            for (const auto& t : traces) {
                if (t.seconds.empty()) continue;
                const std::size_t m = std::min(np, t.seconds.size());
                const auto f = auc::proc::fitLine(rad.first(m), std::span<const double>(t.seconds).first(m));
                if (!f.valid || f.n < 3) continue;
                slopes.push_back(f.slope);
                r2s.push_back(f.r2);
                offsets.push_back(f.intercept + f.slope * rad.front() - t.start());
                ss += f.rms * f.rms * double(f.n);
                nres += f.n;
            }
            QString fitText;
            if (!slopes.empty() && np > 1) {
                const double b = median(slopes), off = median(offsets);
                std::vector<double> steps;
                for (std::size_t j = 1; j < np; ++j) steps.push_back(rad[j] - rad[j - 1]);
                const double step = median(steps);
                const auto [bLo, bHi] = std::minmax_element(slopes.begin(), slopes.end());
                const double r2min = *std::min_element(r2s.begin(), r2s.end());
                std::vector<float> fitY(np);
                for (std::size_t j = 0; j < np; ++j) fitY[j] = float(off + b * (rad[j] - rad.front()));
                sw->y.push_back(std::move(fitY));
                sw->colors.push_back(QColor(0x37, 0x41, 0x51));
                sw->ids.push_back(-1);
                sw->labels.push_back(tr("linear fit (median)"));
                fitText = tr("linear fit: %1 s per radial position (Δr %2 cm, %3 s/cm; per sweep %4–%5 s) · R² %6 (min %7) · rms %8 s")
                              .arg(b * step, 0, 'f', 4)
                              .arg(step, 0, 'f', 4)
                              .arg(b, 0, 'f', 2)
                              .arg(*bLo * step, 0, 'f', 4)
                              .arg(*bHi * step, 0, 'f', 4)
                              .arg(median(r2s), 0, 'f', 6)
                              .arg(r2min, 0, 'f', 6)
                              .arg(std::sqrt(ss / double(nres)), 0, 'f', 3);
            }
            sw->computeBounds();
            sp->y.push_back(std::move(rpm));
            sp->colors.push_back(timingColor(0));
            sp->ids.push_back(0);
            sp->labels.push_back(tr("rotor speed"));
            sp->computeBounds();
            m_timing.sweeps = sw;
            m_timing.sweepSpeed = sp;
            QStringList info;
            info << tr("%n sweep log(s)", nullptr, int(nt));
            if (!durations.empty()) info << tr("sweep %1 s (median)").arg(median(durations), 0, 'f', 1);
            if (!pauses.empty()) info << tr("pause between sweeps %1 s (median)").arg(median(pauses), 0, 'f', 1);
            if (rpmHi >= rpmLo) info << tr("speed during sweeps %1–%2 rpm").arg(rpmLo, 0, 'f', 0).arg(rpmHi, 0, 'f', 0);
            m_timing.sweepInfo = info.join(QStringLiteral(" · "));
            if (!fitText.isEmpty()) m_timing.sweepInfo += QStringLiteral("<br>") + fitText;
        }
    }

    auto markersOnly = [](float size, std::size_t count) {
        CurveStyle s;
        s.marker = CurveStyle::Circle;
        s.markerSize = size;
        s.line = CurveStyle::NoLine;
        QHash<int, CurveStyle> styles;
        for (std::size_t k = 0; k < count; ++k) styles.insert(int(k), s);
        return styles;
    };
    if (m_timingPlot) {
        m_timingPlot->setCurveStyles(markersOnly(4.0f, m_timing.interleaved ? m_timing.interleaved->y.size() : 0));
        m_timingPlot->setSeries(m_timing.interleaved, keep);
    }
    if (m_transitionPlot) {
        m_transitionPlot->setCurveStyles(markersOnly(3.0f, m_timing.transitions ? m_timing.transitions->y.size() : 0));
        m_transitionPlot->setSeries(m_timing.transitions, keep);
    }
    if (m_sweepPlot) {
        CurveStyle fitStyle;
        fitStyle.line = CurveStyle::Dash;
        fitStyle.width = 2.0f;
        m_sweepPlot->setCurveStyles({{-1, fitStyle}});
        m_sweepPlot->setSeries(m_timing.sweeps, keep);
    }
    if (m_sweepSpeedPlot) m_sweepSpeedPlot->setSeries(m_timing.sweepSpeed, keep);
    // Cell plots: refresh the registered ones (delegates added for new cells register themselves).
    for (auto it = m_timingCellPlots.begin(); it != m_timingCellPlots.end(); ++it)
        if (it.value()) {
            const std::size_t i = std::size_t(it.key());
            it.value()->setSeries(i < m_timing.cellSeries.size() ? m_timing.cellSeries[i] : nullptr, keep);
        }
    emit scanTimingChanged();
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
    if (on || m_optSpectrum) reprocess(true);
}

void AppController::setSurfaceMode(int m)
{
    const auto mode = m == 1 ? SurfaceMode::RadiusWavelength : SurfaceMode::RadiusTime;
    if (mode == m_surfaceMode) return;
    m_surfaceMode = mode;
    emit surfaceSettingsChanged();
    if (m_surfaceActive) reprocess(true);
}

void AppController::setSurfaceOmega2t(bool on)
{
    if (on == m_surfaceOmega2t) return;
    m_surfaceOmega2t = on;
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
    const QString path = toPath(url);
    if (!writeCsv(d, path, {runInfo(), yLabel()}, c && c->xIsWavelength ? QStringLiteral("wavelength_nm") : QStringLiteral("radius_cm"), &err)) {
        setStatus(tr("Export failed: %1").arg(err));
        return false;
    }
    setStatus(tr("Exported %1 scans to %2").arg(d.scanCount()).arg(QFileInfo(path).fileName()));
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

    const QString path = toPath(target);
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
    const QString path = toPath(url);
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
    const QString path = toPath(url);
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
