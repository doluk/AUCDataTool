// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Channel.h"

#include "auc/MwlFormat.h"
#include "auc/XlFormat.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

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
    d.type = (rawType == DataType::RadialIntensity && absorbanceData) ? DataType::RadialAbsorbance : rawType;
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
    m_spectra.reset();
}

IoResult ChannelSource::readScanMatrices(std::span<const std::size_t> scanIdx, const MatrixFn& fn) const
{
    std::vector<std::vector<float>> rows;
    if (IoResult r = readRows(0, wavelengths.size(), rows); !r.ok()) return r;
    for (std::size_t i : scanIdx)
        if (i < rows.size()) fn(i, rows[i]);
    return {};
}

IoResult ChannelSource::readPointWindows(std::size_t firstPoint, std::size_t count, const MatrixFn& fn) const
{
    const std::size_t np = radius.size(), nwl = wavelengths.size();
    std::vector<std::size_t> all(scans.size());
    std::iota(all.begin(), all.end(), std::size_t(0));
    std::vector<float> w(nwl * count);
    return readScanMatrices(all, [&](std::size_t i, std::span<const float> m) {
        for (std::size_t k = 0; k < nwl; ++k)
            for (std::size_t j = 0; j < count; ++j) {
                const std::size_t q = k * np + firstPoint + j;
                w[k * count + j] = q < m.size() ? m[q] : std::numeric_limits<float>::quiet_NaN();
            }
        fn(i, w);
    });
}

IoResult ChannelSource::scanMatrix(std::size_t scan, std::vector<float>& out) const
{
    if (scan >= scans.size()) return {IoResult::NoData, QStringLiteral("scan index out of range")};
    const std::size_t idx[] = {scan};
    out.clear();
    if (IoResult r = readScanMatrices(idx, [&](std::size_t, std::span<const float> m) { out.assign(m.begin(), m.end()); }); !r.ok())
        return r;
    if (out.empty()) return {IoResult::NoData, QStringLiteral("scan not readable")};
    return {};
}

IoResult ChannelSource::spectra(std::size_t firstPoint, std::size_t count, std::shared_ptr<const Dataset>& out) const
{
    const std::size_t np = radius.size();
    if (np == 0 || wavelengths.empty() || scans.empty()) return {IoResult::NoData, QStringLiteral("no data")};
    firstPoint = std::min(firstPoint, np - 1);
    count = std::clamp<std::size_t>(count, 1, np - firstPoint);
    {
        std::lock_guard lock(m_cacheMutex);
        if (m_spectra && m_spectraFirst == firstPoint && m_spectraCount == count) {
            out = m_spectra;
            return {};
        }
    }
    const std::size_t nwl = wavelengths.size();
    auto d = std::make_shared<Dataset>(emptyDataset(0.0));
    d->radius = wavelengths;
    double r = 0.0;
    for (std::size_t j = firstPoint; j < firstPoint + count; ++j) r += radius[j];
    r /= double(count);
    for (auto& s : d->scans) {
        s.values.assign(nwl, std::numeric_limits<float>::quiet_NaN());
        s.wavelength = 0.0;
        s.deltaR = nwl > 1 ? wavelengths[1] - wavelengths[0] : 0.0;
    }
    const IoResult res = readPointWindows(firstPoint, count, [&](std::size_t i, std::span<const float> w) {
        auto& v = d->scans[i].values;
        for (std::size_t k = 0; k < nwl; ++k) {
            double sum = 0.0;
            int n = 0;
            for (std::size_t j = 0; j < count && k * count + j < w.size(); ++j) {
                const float x = w[k * count + j];
                if (std::isfinite(x)) sum += x, ++n;
            }
            if (n) v[k] = float(sum / n);
        }
    });
    if (!res.ok()) return res;
    // The spectra's "wavelength" field records the radius they were taken at.
    for (auto& s : d->scans) s.wavelength = r;
    std::lock_guard lock(m_cacheMutex);
    m_spectra = d;
    m_spectraFirst = firstPoint;
    m_spectraCount = count;
    out = std::move(d);
    return {};
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

/// Reads and parses the header of an .mwrs/.mw scan file. Only the first 4 KiB are read
/// when they suffice (a scan file of a large run holds megabytes of readings, and a run
/// has thousands of files); layouts that are told apart by trying several parsers fall
/// back to the full probe so that the result does not depend on the probe size.
IoResult readMwlHeader(QFile& f, bool mwrs, const mwl::MwrsRunInfo& run, mwl::ScanHeader& h)
{
    constexpr qint64 kSmallProbe = 4096;
    auto parse = [&](const QByteArray& head) {
        return mwrs ? mwl::parseMwrsHeader(head, f.size(), run, h) : mwl::parseMwHeader(head, f.size(), h);
    };
    const QByteArray small = f.read(kSmallProbe);
    if (small.size() < kSmallProbe) return parse(small);  // whole file
    // .mwrs ≥ 1.1 has one layout; .mw 1.2 is tried before 1.0/1.1, so its success is final.
    if (const IoResult r = parse(small); r.ok() && (mwrs ? run.version >= 1.05 : h.variant == QLatin1String("mw 1.2")))
        return r;
    if (!f.seek(0)) return {IoResult::CannotOpen, f.errorString()};
    return parse(f.read(mwl::kHeaderProbeBytes));
}

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
        checkIntensityScale();

        scans.clear();
        files.clear();
        for (const auto& f : scanFiles) {
            const auto& h = f.header;
            scans.push_back({h.scan, h.temperature, h.rpm, h.setRpm, h.seconds, h.omega2t});
            files << f.path;
        }
    }

    /// Intensity runs are stored ×1 from v1.3 on, but some acquisition versions writing
    /// "1.3" run files store intensities ×10000 like absorbance in v1.4. Detector counts
    /// stay far below 5·10^6 (16-bit ADC; real runs peak around 6·10^4), while ×10000
    /// encoded rows reach 10^8, so a row maximum above 5·10^6 means the ×10000 encoding.
    void checkIntensityScale()
    {
        if (format != SourceFormat::Mwrs || absorbanceData || scanFiles.empty()) return;
        const auto& h0 = scanFiles.front().header;
        if (h0.scale != 1.0 || h0.points <= 0) return;
        QFile f(scanFiles.front().path);
        const std::size_t np = std::size_t(h0.points);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(h0.dataOffset + qint64(h0.wavelengths.size() / 2 * np) * h0.valueBytes)) return;
        const QByteArray b = f.read(qint64(np) * h0.valueBytes);
        if (b.size() != qint64(np) * h0.valueBytes) return;
        std::vector<float> row(np);
        mwl::decodeValues(b.constData(), np, h0.valueBytes, h0.valueType, 1.0, row.data());
        float peak = 0.f;
        for (float x : row) peak = std::max(peak, std::abs(x));
        if (peak <= 5e6f) return;
        for (auto& sf : scanFiles) sf.header.scale = 1e-4;
        formatName += QStringLiteral(" (÷10000)");
    }

    bool refresh() override
    {
        // Re-read headers of all files of this channel in its folder (new scans appear
        // as new files; files being written are skipped until complete).
        const std::size_t before = scanFiles.size();
        const qint64 lastSize = scanFiles.empty() ? 0 : QFileInfo(scanFiles.back().path).size();
        const QStringList pattern = format == SourceFormat::Mwrs ? QStringList{QStringLiteral("*.mwrs")}
                                                                 : QStringList{QStringLiteral("*.mw"), QStringLiteral("*.mw?")};
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
        return readMwlHeader(f, format == SourceFormat::Mwrs, run, h);
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

    IoResult readScanMatrices(std::span<const std::size_t> scanIdx, const MatrixFn& fn) const override
    {
        const std::size_t n = radius.size() * wavelengths.size();
        QByteArray buf;
        std::vector<float> m(n);
        for (std::size_t i : scanIdx) {
            if (i >= scanFiles.size()) continue;
            const auto& sf = scanFiles[i];
            const auto& h = sf.header;
            QFile f(sf.path);
            if (!f.open(QIODevice::ReadOnly)) return {IoResult::CannotOpen, f.errorString()};
            const qint64 bytes = qint64(n) * h.valueBytes;
            if (!f.seek(h.dataOffset)) return {IoResult::NotAucFile, f.errorString()};
            buf.resize(bytes);
            if (f.read(buf.data(), bytes) != bytes)
                return {IoResult::NotAucFile, QStringLiteral("%1: truncated").arg(QFileInfo(sf.path).fileName())};
            mwl::decodeValues(buf.constData(), n, h.valueBytes, h.valueType, h.scale, m.data());
            fn(i, m);
        }
        return {};
    }

    IoResult readPointWindows(std::size_t firstPoint, std::size_t count, const MatrixFn& fn) const override
    {
        // Spectra need `count` points of each wavelength row. The file is mapped and only
        // those values are decoded, instead of reading and decoding every scan completely.
        const std::size_t np = radius.size(), nwl = wavelengths.size();
        std::vector<float> w(nwl * count);
        for (std::size_t i = 0; i < scanFiles.size(); ++i) {
            const auto& sf = scanFiles[i];
            const auto& h = sf.header;
            QFile f(sf.path);
            if (!f.open(QIODevice::ReadOnly)) return {IoResult::CannotOpen, f.errorString()};
            const qint64 bytes = qint64(nwl * np) * h.valueBytes;
            if (f.size() < h.dataOffset + bytes)
                return {IoResult::NotAucFile, QStringLiteral("%1: truncated").arg(QFileInfo(sf.path).fileName())};
            const uchar* p = f.map(h.dataOffset, bytes);
            if (!p) return ChannelSource::readPointWindows(firstPoint, count, fn);  // e.g. Android content URIs
            for (std::size_t k = 0; k < nwl; ++k)
                mwl::decodeValues(reinterpret_cast<const char*>(p) + (k * np + firstPoint) * std::size_t(h.valueBytes), count,
                                  h.valueBytes, h.valueType, h.scale, w.data() + k * count);
            f.unmap(const_cast<uchar*>(p));
            fn(i, w);
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
        rawType = type;
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

// ---------------------------------------------------------------------------------------
// XL ASCII channels: one file per scan, wavelengths from separate files
// ---------------------------------------------------------------------------------------

class XlChannel final : public ChannelSource {
public:
    struct File {
        QString path;
        xl::ScanHeader header;
    };
    DataType type = DataType::RadialAbsorbance;
    int column = 1;                        ///< 1: second column, 2: third (reference intensity)
    std::map<double, std::vector<File>> byWavelength;
    QStringList sourceFolders;             ///< folders the files came from (live refresh)

    IoResult finalize()
    {
        wavelengths.clear();
        files.clear();
        m_lists.clear();
        for (auto& [wl, list] : byWavelength) {
            std::stable_sort(list.begin(), list.end(), [](const File& a, const File& b) {
                if (a.header.seconds != b.header.seconds) return a.header.seconds < b.header.seconds;
                return a.path < b.path;
            });
            wavelengths.push_back(wl);
            m_lists.push_back(&list);
        }
        if (m_lists.empty()) return {IoResult::NoData, QStringLiteral("no scans")};
        absorbanceData = type != DataType::RadialIntensity && type != DataType::WavelengthIntensity;
        rawType = type;
        xIsWavelength = type == DataType::WavelengthAbsorbance || type == DataType::WavelengthIntensity;
        if (type == DataType::Interference) valueLabel = QStringLiteral("Interference (fringes)");
        if (type == DataType::Fluorescence) valueLabel = QStringLiteral("Fluorescence (counts)");
        formatName = QStringLiteral("XL %1").arg(QString::fromStdString(toCode(type)));
        if (type == DataType::RadialIntensity) formatName += column == 2 ? QStringLiteral(" reference") : QStringLiteral(" sample");

        // Common radial grid: equidistant over the first scan's range. XL radii vary from
        // scan to scan; every scan is interpolated onto this grid when read.
        xl::ScanFile first;
        const File& f0 = m_lists.front()->front();
        if (IoResult r = xl::read(f0.path, first); !r.ok()) return r;
        const std::size_t n = first.x.size();
        radius.resize(n);
        const double x0 = first.x.front(), x1 = first.x.back();
        for (std::size_t j = 0; j < n; ++j) radius[j] = n > 1 ? x0 + (x1 - x0) * double(j) / double(n - 1) : x0;
        if (description.isEmpty()) description = f0.header.description;

        std::size_t ns = SIZE_MAX;
        for (const auto* l : m_lists) ns = std::min(ns, l->size());
        scans.clear();
        const auto& l0 = *m_lists.front();
        static const QRegularExpression digits(QStringLiteral("(\\d+)$"));
        for (std::size_t i = 0; i < ns; ++i) {
            const auto& h = l0[i].header;
            const auto m = digits.match(QFileInfo(l0[i].path).completeBaseName());
            const int number = m.hasMatch() ? m.captured(1).toInt() : int(i) + 1;
            scans.push_back({number, h.temperature, h.rpm, 0.0, h.seconds, h.omega2t});
        }
        for (const auto* l : m_lists)
            for (std::size_t i = 0; i < ns; ++i) files << (*l)[i].path;
        return {};
    }

    bool refresh() override
    {
        const std::size_t before = std::size_t(files.size());
        QSet<QString> known;
        for (const auto& [wl, list] : byWavelength)
            for (const auto& f : list) known.insert(f.path);
        for (const QString& dir : sourceFolders) {
            for (const QFileInfo& fi : QDir(dir).entryInfoList(xl::nameFilters(), QDir::Files)) {
                const QString path = fi.absoluteFilePath();
                if (known.contains(path) || !xl::isXlSuffix(fi.suffix())) continue;
                File f{path, {}};
                if (!xl::readHeader(path, f.header).ok() || f.header.type != type || f.header.cell != cell) continue;
                byWavelength[f.header.wavelength].push_back(std::move(f));
            }
        }
        finalize();
        clearCache();
        return std::size_t(files.size()) != before;
    }

protected:
    IoResult readRows(std::size_t first, std::size_t count, std::vector<std::vector<float>>& out) const override
    {
        const std::size_t np = radius.size();
        const std::size_t ns = scans.size();
        out.assign(ns, std::vector<float>(count * np, std::numeric_limits<float>::quiet_NaN()));
        xl::ScanFile sf;
        for (std::size_t k = 0; k < count && first + k < m_lists.size(); ++k) {
            const auto& list = *m_lists[first + k];
            for (std::size_t i = 0; i < ns && i < list.size(); ++i) {
                if (IoResult r = xl::read(list[i].path, sf); !r.ok())
                    return {r.code, QStringLiteral("%1: %2").arg(QFileInfo(list[i].path).fileName(), r.message)};
                const auto& y = (column == 2 && !sf.third.empty()) ? sf.third : sf.value;
                xl::resample(sf.x, y, radius, out[i].data() + k * np);
            }
        }
        return {};
    }

private:
    std::vector<const std::vector<File>*> m_lists;  ///< per wavelength, ascending
};

/// Channel letter of an XL file: "A00110.RA2" -> A, folder "3B410" -> B, otherwise A.
char xlChannelLetter(const QFileInfo& fi)
{
    static const QRegularExpression fileRe(QStringLiteral("^([A-HRS])\\d+$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression dirRe(QStringLiteral("^\\d+([A-HRS])\\d+$"), QRegularExpression::CaseInsensitiveOption);
    if (const auto m = fileRe.match(fi.completeBaseName()); m.hasMatch()) return m.captured(1).toUpper().at(0).toLatin1();
    if (const auto m = dirRe.match(fi.dir().dirName()); m.hasMatch()) return m.captured(1).toUpper().at(0).toLatin1();
    return 'A';
}

/// Wavelength folders of one run ("2A280", "2A230", "1S0") are grouped by their parent.
bool isWavelengthFolder(const QString& dirName)
{
    static const QRegularExpression re(QStringLiteral("^\\d+[A-HRS]\\d+$"), QRegularExpression::CaseInsensitiveOption);
    return re.match(dirName).hasMatch();
}

bool isMwSuffix(const QString& suffix)
{
    // ".mw" or ".MW3" (cell number appended by the acquisition software), not ".mwrs".
    return suffix.compare(QLatin1String("mw"), Qt::CaseInsensitive) == 0
        || (suffix.size() == 3 && suffix.startsWith(QLatin1String("mw"), Qt::CaseInsensitive) && suffix[2].isDigit());
}

void collectFiles(const QString& path, QStringList& out)
{
    const QFileInfo fi(path);
    if (fi.isFile()) {
        out << fi.absoluteFilePath();
        return;
    }
    if (!fi.isDir()) return;
    QStringList filters{QStringLiteral("*.auc"), QStringLiteral("*.mwrs"), QStringLiteral("*.mw"), QStringLiteral("*.mw?")};
    filters << xl::nameFilters();
    QDirIterator it(fi.absoluteFilePath(), filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
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
    std::map<QString, std::shared_ptr<XlChannel>> xls;

    for (const QString& path : all) {
        const QFileInfo fi(path);
        const QString folder = fi.absolutePath();
        const QString suffix = fi.suffix().toLower();

        if (suffix == QLatin1String("mwrs") || isMwSuffix(suffix)) {
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
            const IoResult r = readMwlHeader(f, isMwrs, run, h);
            if (!r.ok()) {
                res.warnings << QStringLiteral("%1: %2").arg(fi.fileName(), r.message);
                continue;
            }
            const QString key = QStringLiteral("%1|%2|%3|%4")
                                    .arg(folder, isMwrs ? suffix : QStringLiteral("mw"))
                                    .arg(h.cell)
                                    .arg(QLatin1Char(h.channel));
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
        } else if (xl::isXlSuffix(fi.suffix())) {
            xl::ScanHeader h;
            if (IoResult r = xl::readHeader(path, h); !r.ok()) {
                res.warnings << QStringLiteral("%1: %2").arg(fi.fileName(), r.message);
                continue;
            }
            const bool grouped = isWavelengthFolder(fi.dir().dirName());
            const QString group = grouped ? QFileInfo(folder).absolutePath() : folder;
            const char letter = xlChannelLetter(fi);
            // Intensity files of double-sector cells: second column sample, third reference.
            const bool withReference = h.type == DataType::RadialIntensity && h.columns >= 3;
            for (int column = 1; column <= (withReference ? 2 : 1); ++column) {
                char channel = letter;
                if (column == 2) channel = letter == 'A' ? 'B' : 'R';
                const QString key = QStringLiteral("%1|%2|%3|%4")
                                        .arg(group, QString::fromStdString(toCode(h.type)))
                                        .arg(h.cell)
                                        .arg(QLatin1Char(channel));
                auto& ch = xls[key];
                if (!ch) {
                    ch = std::make_shared<XlChannel>();
                    ch->format = SourceFormat::Xl;
                    ch->type = h.type;
                    ch->column = column;
                    ch->cell = h.cell;
                    ch->channel = channel;
                    ch->folder = group;
                    ch->runId = QDir(group).dirName();
                }
                if (!ch->sourceFolders.contains(folder)) ch->sourceFolders << folder;
                ch->byWavelength[h.wavelength].push_back({path, h});
            }
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
    for (auto& [key, ch] : xls) {
        if (IoResult r = ch->finalize(); !r.ok()) {
            res.warnings << QStringLiteral("%1 %2: %3").arg(ch->runId, ch->key(), r.message);
            continue;
        }
        if (!ch->scans.empty()) res.channels.push_back(ch);
    }
    std::sort(res.channels.begin(), res.channels.end(), [](const ChannelPtr& a, const ChannelPtr& b) {
        if (a->folder != b->folder) return a->folder < b->folder;
        if (a->cell != b->cell) return a->cell < b->cell;
        return a->channel < b->channel;
    });
    return res;
}

}  // namespace auc
