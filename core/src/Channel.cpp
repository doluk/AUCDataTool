// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Channel.h"

#include "auc/MwlFormat.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <map>

namespace auc {

// ---------------------------------------------------------------------------------------
// ChannelSource
// ---------------------------------------------------------------------------------------

QString ChannelSource::key() const { return QStringLiteral("%1%2").arg(cell).arg(QLatin1Char(channel)); }

std::size_t ChannelSource::nearestWavelength(double nm) const
{
    if (wavelengths.empty()) return 0;
    const auto it = std::lower_bound(wavelengths.begin(), wavelengths.end(), nm);
    if (it == wavelengths.begin()) return 0;
    if (it == wavelengths.end()) return wavelengths.size() - 1;
    const std::size_t i = std::size_t(it - wavelengths.begin());
    return (nm - wavelengths[i - 1] <= wavelengths[i] - nm) ? i - 1 : i;
}

Dataset ChannelSource::emptyDataset(double wavelength) const
{
    Dataset d;
    d.type = absorbanceData ? DataType::RadialAbsorbance : DataType::RadialIntensity;
    d.cell = cell;
    d.channel = channel;
    d.description = description.toStdString();
    d.radius = radius;
    d.scans.resize(scans.size());
    const double dr = radius.size() > 1 ? radius[1] - radius[0] : 0.0;
    for (std::size_t i = 0; i < scans.size(); ++i) {
        Scan& s = d.scans[i];
        s.temperature = scans[i].temperature;
        s.rpm = scans[i].rpm;
        s.seconds = scans[i].seconds;
        s.omega2t = scans[i].omega2t;
        s.wavelength = wavelength;
        s.deltaR = dr;
    }
    return d;
}

void ChannelSource::clearCache() const
{
    std::lock_guard lock(m_cacheMutex);
    m_cache.clear();
}

IoResult ChannelSource::wavelengthSlice(std::size_t index, std::shared_ptr<const Dataset>& out) const
{
    return wavelengthMean(index, 1, out);
}

IoResult ChannelSource::wavelengthMean(std::size_t first, std::size_t count, std::shared_ptr<const Dataset>& out) const
{
    if (wavelengths.empty() || first >= wavelengths.size())
        return {IoResult::NoData, QStringLiteral("wavelength index out of range")};
    count = std::clamp<std::size_t>(count, 1, wavelengths.size() - first);
    {
        std::lock_guard lock(m_cacheMutex);
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->first == first && it->count == count) {
                m_cache.splice(m_cache.begin(), m_cache, it);
                out = m_cache.front().data;
                return {};
            }
        }
    }

    std::vector<std::vector<float>> rows;
    if (IoResult r = readRows(first, count, rows); !r.ok()) return r;

    double meanWl = 0.0;
    for (std::size_t k = 0; k < count; ++k) meanWl += wavelengths[first + k];
    meanWl /= double(count);

    auto d = std::make_shared<Dataset>(emptyDataset(meanWl));
    const std::size_t np = radius.size();
    const std::size_t ns = std::min(rows.size(), d->scans.size());
    d->scans.resize(ns);
    for (std::size_t i = 0; i < ns; ++i) {
        auto& v = d->scans[i].values;
        v.assign(np, 0.f);
        const auto& src = rows[i];
        for (std::size_t k = 0; k < count; ++k)
            for (std::size_t j = 0; j < np && k * np + j < src.size(); ++j) v[j] += src[k * np + j];
        if (count > 1)
            for (float& x : v) x /= float(count);
    }

    std::lock_guard lock(m_cacheMutex);
    m_cache.push_front({first, count, d});
    if (m_cache.size() > kCacheSize) m_cache.pop_back();
    out = std::move(d);
    return {};
}

namespace {

// ---------------------------------------------------------------------------------------
// MWL scan-file channels (.mwrs, .mw)
// ---------------------------------------------------------------------------------------

struct ScanFile {
    QString path;
    mwl::ScanHeader header;
};

class MwlChannel final : public ChannelSource {
public:
    std::vector<ScanFile> scanFiles;
    mwl::MwrsRunInfo run;  // .mwrs only
    QStringList skipped;

    /// Builds metadata from the scan files (sorted by scan number). Files whose layout
    /// differs from the first are skipped.
    void finalize()
    {
        std::sort(scanFiles.begin(), scanFiles.end(),
                  [](const ScanFile& a, const ScanFile& b) { return a.header.scan < b.header.scan; });
        if (scanFiles.empty()) return;
        const int points0 = scanFiles.front().header.points;
        const std::size_t nwl0 = scanFiles.front().header.wavelengths.size();
        std::vector<ScanFile> ok;
        for (auto& f : scanFiles) {
            const auto& h = f.header;
            if (h.points != points0 || h.wavelengths.size() != nwl0) {
                skipped << QFileInfo(f.path).fileName();
                continue;
            }
            ok.push_back(std::move(f));
        }
        scanFiles = std::move(ok);
        if (scanFiles.empty()) return;
        const auto& h0 = scanFiles.front().header;

        wavelengths = h0.wavelengths;
        radius.resize(size_t(h0.points));
        for (int j = 0; j < h0.points; ++j) radius[size_t(j)] = h0.rStart + j * h0.rStep;
        darkCurrent = h0.darkCurrent;
        darkSubtractedInFile = h0.darkSubtracted;
        formatName = h0.variant;
        if (description.isEmpty()) description = h0.description;

        scans.clear();
        files.clear();
        for (const auto& f : scanFiles) {
            const auto& h = f.header;
            scans.push_back({h.scan, h.temperature, h.rpm, h.setRpm, h.seconds, h.omega2t});
            files << f.path;
        }
    }

    bool refresh() override
    {
        // Re-read headers of all files of this channel in its folder (new scans appear
        // as new files; files being written are skipped until complete).
        const std::size_t before = scanFiles.size();
        const qint64 lastSize = scanFiles.empty() ? 0 : QFileInfo(scanFiles.back().path).size();
        const QStringList pattern{format == SourceFormat::Mwrs ? QStringLiteral("*.mwrs") : QStringLiteral("*.mw")};
        std::vector<ScanFile> found;
        for (const QFileInfo& fi : QDir(folder).entryInfoList(pattern, QDir::Files)) {
            ScanFile sf;
            sf.path = fi.absoluteFilePath();
            if (!readHeader(sf.path, sf.header).ok()) continue;
            if (sf.header.cell != cell || sf.header.channel != channel) continue;
            found.push_back(std::move(sf));
        }
        scanFiles = std::move(found);
        finalize();
        clearCache();
        return scanFiles.size() != before
            || (!scanFiles.empty() && QFileInfo(scanFiles.back().path).size() != lastSize);
    }

    IoResult readHeader(const QString& path, mwl::ScanHeader& h) const
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {IoResult::CannotOpen, f.errorString()};
        const QByteArray head = f.read(mwl::kHeaderProbeBytes);
        return format == SourceFormat::Mwrs ? mwl::parseMwrsHeader(head, f.size(), run, h)
                                            : mwl::parseMwHeader(head, f.size(), h);
    }

protected:
    IoResult readRows(std::size_t first, std::size_t count, std::vector<std::vector<float>>& out) const override
    {
        const std::size_t np = radius.size();
        out.assign(scanFiles.size(), {});
        QByteArray buf;
        for (std::size_t i = 0; i < scanFiles.size(); ++i) {
            const auto& sf = scanFiles[i];
            const auto& h = sf.header;
            QFile f(sf.path);
            if (!f.open(QIODevice::ReadOnly)) return {IoResult::CannotOpen, f.errorString()};
            const qint64 bytes = qint64(count * np) * h.valueBytes;
            if (!f.seek(h.dataOffset + qint64(first * np) * h.valueBytes)) return {IoResult::NotAucFile, f.errorString()};
            buf.resize(bytes);
            if (f.read(buf.data(), bytes) != bytes)
                return {IoResult::NotAucFile, QStringLiteral("%1: truncated").arg(QFileInfo(sf.path).fileName())};
            out[i].resize(count * np);
            mwl::decodeValues(buf.constData(), count * np, h.valueBytes, h.valueType, h.scale, out[i].data());
        }
        return {};
    }
};

// ---------------------------------------------------------------------------------------
// .auc channels: one file per wavelength
// ---------------------------------------------------------------------------------------

class AucChannel final : public ChannelSource {
public:
    struct WlFile {
        QString path;
        double wavelength;
        int scanCount;
    };
    std::vector<WlFile> wlFiles;
    DataType type = DataType::RadialAbsorbance;

    IoResult finalize()
    {
        std::sort(wlFiles.begin(), wlFiles.end(), [](const WlFile& a, const WlFile& b) { return a.wavelength < b.wavelength; });
        wavelengths.clear();
        files.clear();
        for (const auto& w : wlFiles) {
            wavelengths.push_back(w.wavelength);
            files << w.path;
        }
        absorbanceData = type != DataType::RadialIntensity && type != DataType::WavelengthIntensity;
        formatName = QStringLiteral("auc %1").arg(QString::fromStdString(toCode(type)));
        // Radius grid and run conditions from the first file.
        Dataset d;
        if (IoResult r = AucFile::read(wlFiles.front().path, d); !r.ok()) return r;
        radius = d.radius;
        if (description.isEmpty()) description = QString::fromStdString(d.description);
        int minScans = int(d.scanCount());
        for (const auto& w : wlFiles) minScans = std::min(minScans, w.scanCount);
        scans.clear();
        for (int i = 0; i < minScans; ++i) {
            const Scan& s = d.scans[size_t(i)];
            scans.push_back({i + 1, s.temperature, s.rpm, 0.0, s.seconds, s.omega2t});
        }
        return {};
    }

    bool refresh() override
    {
        bool changed = false;
        for (auto& w : wlFiles) {
            AucFile::Header h;
            if (AucFile::readHeader(w.path, h).ok() && h.scanCount != w.scanCount) {
                w.scanCount = h.scanCount;
                changed = true;
            }
        }
        if (changed) {
            finalize();
            clearCache();
        }
        return changed;
    }

protected:
    IoResult readRows(std::size_t first, std::size_t count, std::vector<std::vector<float>>& out) const override
    {
        const std::size_t np = radius.size();
        const std::size_t ns = scans.size();
        out.assign(ns, std::vector<float>(count * np, 0.f));
        for (std::size_t k = 0; k < count; ++k) {
            Dataset d;
            if (IoResult r = AucFile::read(wlFiles[first + k].path, d); !r.ok()) return r;
            if (d.pointCount() != np)
                return {IoResult::NotAucFile, QStringLiteral("%1: radius grid differs").arg(QFileInfo(wlFiles[first + k].path).fileName())};
            for (std::size_t i = 0; i < ns && i < d.scans.size(); ++i)
                std::copy(d.scans[i].values.begin(), d.scans[i].values.end(), out[i].begin() + std::ptrdiff_t(k * np));
        }
        return {};
    }
};

void collectFiles(const QString& path, QStringList& out)
{
    const QFileInfo fi(path);
    if (fi.isFile()) {
        out << fi.absoluteFilePath();
        return;
    }
    if (!fi.isDir()) return;
    QDirIterator it(fi.absoluteFilePath(), {QStringLiteral("*.auc"), QStringLiteral("*.mwrs"), QStringLiteral("*.mw")},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    const int baseDepth = fi.absoluteFilePath().count(QLatin1Char('/'));
    while (it.hasNext()) {
        const QString p = it.next();
        if (p.count(QLatin1Char('/')) - baseDepth <= 4) out << p;  // up to 3 sub-levels
    }
}

}  // namespace

OpenResult openData(const QStringList& paths)
{
    OpenResult res;
    QStringList all;
    for (const QString& p : paths) collectFiles(p, all);
    all.removeDuplicates();
    all.sort();

    // .mwrs: per folder, run settings from the folder's *.mwrs.xml
    std::map<QString, std::shared_ptr<MwlChannel>> mwlChannels;  // key: folder|format|cell|channel
    QHash<QString, mwl::MwrsRunInfo> runs;
    std::map<QString, std::shared_ptr<AucChannel>> aucs;

    for (const QString& path : all) {
        const QFileInfo fi(path);
        const QString folder = fi.absolutePath();
        const QString suffix = fi.suffix().toLower();

        if (suffix == QLatin1String("mwrs") || suffix == QLatin1String("mw")) {
            const bool isMwrs = suffix == QLatin1String("mwrs");
            mwl::MwrsRunInfo run;
            if (isMwrs) {
                if (!runs.contains(folder)) {
                    mwl::MwrsRunInfo info;
                    const QStringList xmls = QDir(folder).entryList({QStringLiteral("*.mwrs.xml")}, QDir::Files);
                    if (xmls.isEmpty()) {
                        res.warnings << QStringLiteral("%1: no *.mwrs.xml – assuming version 1.4, intensity data").arg(folder);
                    } else if (IoResult r = mwl::readMwrsXml(QDir(folder).filePath(xmls.first()), info); !r.ok()) {
                        res.warnings << QStringLiteral("%1: %2").arg(xmls.first(), r.message);
                    }
                    runs.insert(folder, info);
                }
                run = runs.value(folder);
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                res.warnings << QStringLiteral("%1: %2").arg(fi.fileName(), f.errorString());
                continue;
            }
            mwl::ScanHeader h;
            const QByteArray head = f.read(mwl::kHeaderProbeBytes);
            const IoResult r = isMwrs ? mwl::parseMwrsHeader(head, f.size(), run, h) : mwl::parseMwHeader(head, f.size(), h);
            if (!r.ok()) {
                res.warnings << QStringLiteral("%1: %2").arg(fi.fileName(), r.message);
                continue;
            }
            const QString key = QStringLiteral("%1|%2|%3|%4").arg(folder, suffix).arg(h.cell).arg(QLatin1Char(h.channel));
            auto& ch = mwlChannels[key];
            if (!ch) {
                ch = std::make_shared<MwlChannel>();
                ch->format = isMwrs ? SourceFormat::Mwrs : SourceFormat::Mw;
                ch->cell = h.cell;
                ch->channel = h.channel;
                ch->folder = folder;
                ch->run = run;
                ch->runId = run.runId.isEmpty() ? QDir(folder).dirName() : run.runId;
                ch->absorbanceData = isMwrs && !run.takeIntensity && run.version >= 1.35;
                ch->description = run.samples.value(ch->key());
            }
            ch->scanFiles.push_back({path, std::move(h)});
        } else if (suffix == QLatin1String("auc")) {
            AucFile::Header h;
            if (IoResult r = AucFile::readHeader(path, h); !r.ok()) {
                res.warnings << QStringLiteral("%1: %2").arg(fi.fileName(), r.message);
                continue;
            }
            // UltraScan names: run.TYPE.cell.channel.wavelength.auc → run id = first part
            const QString runId = fi.fileName().section(QLatin1Char('.'), 0, 0);
            const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                                    .arg(folder, runId, QString::fromStdString(toCode(h.type)))
                                    .arg(h.cell)
                                    .arg(QLatin1Char(h.channel));
            auto& ch = aucs[key];
            if (!ch) {
                ch = std::make_shared<AucChannel>();
                ch->format = SourceFormat::Auc;
                ch->type = h.type;
                ch->cell = h.cell;
                ch->channel = h.channel;
                ch->folder = folder;
                ch->runId = runId;
                ch->description = h.description;
            }
            ch->wlFiles.push_back({path, h.wavelength, h.scanCount});
        }
    }

    for (auto& [key, ch] : mwlChannels) {
        ch->finalize();
        if (!ch->skipped.isEmpty())
            res.warnings << QStringLiteral("%1 %2: skipped %3 file(s) with a different layout")
                                .arg(ch->runId, ch->key())
                                .arg(ch->skipped.size());
        if (!ch->scans.empty()) res.channels.push_back(ch);
    }
    for (auto& [key, ch] : aucs) {
        if (IoResult r = ch->finalize(); !r.ok()) {
            res.warnings << QStringLiteral("%1 %2: %3").arg(ch->runId, ch->key(), r.message);
            continue;
        }
        res.channels.push_back(ch);
    }
    std::sort(res.channels.begin(), res.channels.end(), [](const ChannelPtr& a, const ChannelPtr& b) {
        if (a->folder != b->folder) return a->folder < b->folder;
        if (a->cell != b->cell) return a->cell < b->cell;
        return a->channel < b->channel;
    });
    return res;
}

}  // namespace auc
