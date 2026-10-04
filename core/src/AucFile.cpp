// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/AucFile.h"

#include "Crc32.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace auc {

namespace {

constexpr int kOldestVersion = 4;
constexpr int kDescriptionBytes = 240;
constexpr qsizetype kHeaderBytes = 4 + 2 + 2 + 1 + 1 + 16 + kDescriptionBytes + 7 * 4 + 2;
constexpr qsizetype kScanHeaderBytes = 4 + 4 + 4 + 4 + 4 + 2 + 4 + 4;

/// Bounds-checked little-endian cursor that accumulates the file CRC as it reads.
class Reader {
public:
    explicit Reader(const QByteArray& bytes)
        : m_data(reinterpret_cast<const unsigned char*>(bytes.constData())), m_size(bytes.size())
    {
    }

    bool take(void* dst, qsizetype n)
    {
        if (n < 0 || m_pos + n > m_size) return false;
        std::memcpy(dst, m_data + m_pos, static_cast<size_t>(n));
        m_crc = detail::crc32(m_crc, m_data + m_pos, static_cast<size_t>(n));
        m_pos += n;
        return true;
    }
    template <typename T>
    bool le(T& v)
    {
        unsigned char b[sizeof(T)];
        if (!take(b, sizeof(T))) return false;
        v = qFromLittleEndian<T>(b);
        return true;
    }
    bool f32(float& v)
    {
        quint32 bits;
        if (!le(bits)) return false;
        std::memcpy(&v, &bits, sizeof v);
        return true;
    }
    qsizetype remaining() const { return m_size - m_pos; }
    quint32 crc() const { return m_crc; }
    /// Reads the trailing CRC without folding it into the running value.
    bool trailingCrc(quint32& v)
    {
        if (m_pos + 4 > m_size) return false;
        v = qFromLittleEndian<quint32>(m_data + m_pos);
        m_pos += 4;
        return true;
    }

private:
    const unsigned char* m_data;
    qsizetype m_size;
    qsizetype m_pos = 0;
    quint32 m_crc = 0xFFFFFFFFu;
};

class Writer {
public:
    void put(const void* src, qsizetype n)
    {
        m_crc = detail::crc32(m_crc, src, static_cast<size_t>(n));
        m_bytes.append(static_cast<const char*>(src), n);
    }
    template <typename T>
    void le(T v)
    {
        unsigned char b[sizeof(T)];
        qToLittleEndian<T>(v, b);
        put(b, sizeof(T));
    }
    void f32(float v)
    {
        quint32 bits;
        std::memcpy(&bits, &v, sizeof v);
        le(bits);
    }
    QByteArray finish()
    {
        unsigned char b[4];
        qToLittleEndian<quint32>(m_crc, b);
        m_bytes.append(reinterpret_cast<const char*>(b), 4);
        return m_bytes;
    }

private:
    QByteArray m_bytes;
    quint32 m_crc = 0xFFFFFFFFu;
};

IoResult fail(IoResult::Code c, const QString& msg) { return {c, msg}; }

IoResult parseHeader(Reader& r, AucFile::Header& h, float (&ranges)[7])
{
    char magic[4];
    if (!r.take(magic, 4) || std::memcmp(magic, "UCDA", 4) != 0)
        return fail(IoResult::NotAucFile, QStringLiteral("missing UCDA magic"));

    unsigned char ver[2];
    if (!r.take(ver, 2)) return fail(IoResult::NotAucFile, QStringLiteral("truncated header"));
    h.version = ((ver[0] & 0x0F) << 8) | (ver[1] & 0x0F);  // same decoding as openAUC readers
    if (h.version < kOldestVersion || h.version > AucFile::kWriteVersion)
        return fail(IoResult::BadVersion, QStringLiteral("unsupported format version %1").arg(h.version));

    char type[3] = {};
    if (!r.take(type, 2) || !fromCode(type, h.type))
        return fail(IoResult::BadType, QStringLiteral("unknown data type '%1'").arg(QLatin1String(type, 2)));

    quint8 cell;
    char channel;
    unsigned char guid[16];
    char desc[kDescriptionBytes + 1] = {};
    if (!r.le(cell) || !r.take(&channel, 1) || !r.take(guid, 16) || !r.take(desc, kDescriptionBytes))
        return fail(IoResult::NotAucFile, QStringLiteral("truncated header"));
    h.cell = cell;
    h.channel = channel;
    std::memcpy(h.guid.data(), guid, 16);
    h.description = QString::fromLatin1(desc, static_cast<qsizetype>(std::strlen(desc))).trimmed();

    for (float& v : ranges)
        if (!r.f32(v)) return fail(IoResult::NotAucFile, QStringLiteral("truncated header"));

    qint16 count;
    if (!r.le(count)) return fail(IoResult::NotAucFile, QStringLiteral("truncated header"));
    if (count <= 0) return fail(IoResult::NoData, QStringLiteral("file contains no scans"));
    h.scanCount = count;
    return {};
}

}  // namespace

IoResult AucFile::readHeader(const QString& path, Header& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    const QByteArray head = f.read(kHeaderBytes + kScanHeaderBytes);
    Reader r(head);
    float ranges[7];
    Header h;
    if (IoResult res = parseHeader(r, h, ranges); !res.ok()) return res;
    h.rMin = ranges[0];
    h.deltaR = ranges[2];
    char tag[4];
    float temp, rpm, w2t, dr;
    qint32 seconds, count;
    quint16 wl;
    if (r.take(tag, 4) && std::memcmp(tag, "DATA", 4) == 0 && r.f32(temp) && r.f32(rpm) && r.le(seconds) && r.f32(w2t)
        && r.le(wl) && r.f32(dr) && r.le(count)) {
        h.wavelength = h.version > 4 ? wl / 10.0 : wl / 100.0 + 180.0;
        h.points = count;
    }
    out = std::move(h);
    return {};
}

IoResult AucFile::read(const QString& path, Dataset& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    return readFromBytes(f.readAll(), out);
}

IoResult AucFile::readFromBytes(const QByteArray& bytes, Dataset& out)
{
    Reader r(bytes);
    Header h;
    float ranges[7];  // rMin, rMax, dR, d1min, d1max, d2min, d2max
    if (IoResult res = parseHeader(r, h, ranges); !res.ok()) return res;

    const double rMin = ranges[0], dR = ranges[2];
    if (!std::isfinite(rMin) || !std::isfinite(dR) || dR <= 0.0)
        return fail(IoResult::NotAucFile, QStringLiteral("invalid radius axis"));
    if (static_cast<qint64>(h.scanCount) * kScanHeaderBytes > r.remaining())
        return fail(IoResult::NotAucFile, QStringLiteral("scan count exceeds file size"));

    const double d1min = ranges[3], d1max = ranges[4], d2min = ranges[5], d2max = ranges[6];
    const double f1 = (d1max - d1min) / 65535.0;
    const double f2 = (d2max - d2min) / 65535.0;
    const bool hasStdDev = d2min != 0.0 || d2max != 0.0;
    const bool newWavelength = h.version > 4;

    Dataset d;
    d.type = h.type;
    d.cell = h.cell;
    d.channel = h.channel;
    d.guid = h.guid;
    d.description = h.description.toStdString();
    d.scans.reserve(static_cast<size_t>(h.scanCount));

    qint32 valueCount = 0;
    for (int i = 0; i < h.scanCount; ++i) {
        char tag[4];
        float temp, rpm, w2t, scanDr;
        qint32 seconds, count;
        quint16 wl;
        if (!r.take(tag, 4) || std::memcmp(tag, "DATA", 4) != 0)
            return fail(IoResult::NotAucFile, QStringLiteral("scan %1: missing DATA tag").arg(i));
        if (!r.f32(temp) || !r.f32(rpm) || !r.le(seconds) || !r.f32(w2t) || !r.le(wl) || !r.f32(scanDr)
            || !r.le(count))
            return fail(IoResult::NotAucFile, QStringLiteral("scan %1: truncated header").arg(i));
        if (count <= 0) return fail(IoResult::NoData, QStringLiteral("scan %1 has no readings").arg(i));
        if (i == 0)
            valueCount = count;
        else if (count != valueCount)
            return fail(IoResult::NotAucFile, QStringLiteral("scan %1: reading count differs").arg(i));

        const qint64 need = qint64(valueCount) * (hasStdDev ? 4 : 2) + (qint64(valueCount) + 7) / 8;
        if (need > r.remaining())
            return fail(IoResult::NotAucFile, QStringLiteral("scan %1: truncated readings").arg(i));

        Scan s;
        s.temperature = temp;
        s.rpm = rpm;
        s.seconds = seconds;
        s.omega2t = w2t;
        s.wavelength = newWavelength ? wl / 10.0 : wl / 100.0 + 180.0;
        s.deltaR = scanDr;
        s.values.resize(static_cast<size_t>(valueCount));
        if (hasStdDev) s.stddev.resize(static_cast<size_t>(valueCount));
        for (qint32 j = 0; j < valueCount; ++j) {
            quint16 q = 0;
            r.le(q);  // length checked above
            s.values[size_t(j)] = static_cast<float>(q * f1 + d1min);
            if (hasStdDev) {
                r.le(q);
                s.stddev[size_t(j)] = static_cast<float>(q * f2 + d2min);
            }
        }
        s.interpolated.resize(static_cast<size_t>((valueCount + 7) / 8));
        r.take(s.interpolated.data(), qsizetype(s.interpolated.size()));
        d.scans.push_back(std::move(s));
    }

    d.radius.resize(static_cast<size_t>(valueCount));
    for (qint32 j = 0; j < valueCount; ++j)
        d.radius[size_t(j)] = rMin + j * dR;  // stored axis is uniform

    const quint32 computed = r.crc();
    quint32 stored;
    if (!r.trailingCrc(stored)) return fail(IoResult::NotAucFile, QStringLiteral("missing CRC"));
    if (stored != computed) return fail(IoResult::BadCrc, QStringLiteral("CRC mismatch – file is corrupt"));

    out = std::move(d);
    return {};
}

QByteArray AucFile::toBytes(const Dataset& data)
{
    Writer w;
    w.put("UCDA", 4);
    char ver[3];
    std::snprintf(ver, sizeof ver, "%02d", kWriteVersion);
    w.put(ver, 2);
    const std::string code = toCode(data.type);
    w.put(code.data(), 2);
    w.le<quint8>(static_cast<quint8>(data.cell));
    w.put(&data.channel, 1);
    w.put(data.guid.data(), 16);
    char desc[kDescriptionBytes] = {};
    std::memcpy(desc, data.description.data(), std::min<size_t>(data.description.size(), kDescriptionBytes));
    w.put(desc, kDescriptionBytes);

    // Quantisation ranges over all scans.
    float d1min = std::numeric_limits<float>::max(), d1max = std::numeric_limits<float>::lowest();
    float d2min = 0.f, d2max = 0.f;
    bool hasStdDev = false;
    for (const auto& s : data.scans) {
        for (float v : s.values) {
            d1min = std::min(d1min, v);
            d1max = std::max(d1max, v);
        }
        if (!s.stddev.empty()) {
            if (!hasStdDev) {
                d2min = std::numeric_limits<float>::max();
                d2max = std::numeric_limits<float>::lowest();
                hasStdDev = true;
            }
            for (float v : s.stddev) {
                d2min = std::min(d2min, v);
                d2max = std::max(d2max, v);
            }
        }
    }
    if (d1min > d1max) d1min = d1max = 0.f;
    // A zero range would make every value decode to the minimum; widen it slightly.
    if (d1max == d1min) d1max = d1min + 1.f;
    if (hasStdDev && d2max == d2min) d2max = d2min + 1.f;
    if (hasStdDev && d2min == 0.f && d2max == 0.f) d2max = 1.f;

    const double rMin = data.radius.empty() ? 0.0 : data.radius.front();
    const double rMax = data.radius.empty() ? 0.0 : data.radius.back();
    const double dR = data.radius.size() > 1 ? data.radius[1] - data.radius[0] : 0.0;
    for (float v : {float(rMin), float(rMax), float(dR), d1min, d1max, d2min, d2max})
        w.f32(v);
    w.le<qint16>(static_cast<qint16>(data.scans.size()));

    auto quant = [](float v, float lo, float hi) {
        const double q = std::round((double(v) - lo) / (double(hi) - lo) * 65535.0);
        return static_cast<quint16>(std::clamp(q, 0.0, 65535.0));
    };

    const size_t n = data.radius.size();
    for (const auto& s : data.scans) {
        w.put("DATA", 4);
        w.f32(float(s.temperature));
        w.f32(float(s.rpm));
        w.le<qint32>(static_cast<qint32>(std::lround(s.seconds)));
        w.f32(float(s.omega2t));
        w.le<quint16>(static_cast<quint16>(std::lround(s.wavelength * 10.0)));
        w.f32(float(s.deltaR > 0 ? s.deltaR : dR));
        w.le<qint32>(static_cast<qint32>(n));
        for (size_t j = 0; j < n; ++j) {
            w.le<quint16>(quant(j < s.values.size() ? s.values[j] : 0.f, d1min, d1max));
            if (hasStdDev) w.le<quint16>(quant(j < s.stddev.size() ? s.stddev[j] : d2min, d2min, d2max));
        }
        std::vector<std::uint8_t> bits((n + 7) / 8, 0);
        std::copy_n(s.interpolated.begin(), std::min(bits.size(), s.interpolated.size()), bits.begin());
        w.put(bits.data(), qsizetype(bits.size()));
    }
    return w.finish();
}

IoResult AucFile::write(const QString& path, const Dataset& data)
{
    if (data.radius.size() < 2) return fail(IoResult::NoData, QStringLiteral("need at least two radius points"));
    if (data.scans.empty()) return fail(IoResult::NoData, QStringLiteral("no scans to write"));
    if (data.scans.size() > size_t(std::numeric_limits<qint16>::max()))
        return fail(IoResult::WriteFailed, QStringLiteral("too many scans for the format"));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(IoResult::CannotOpen, f.errorString());
    const QByteArray bytes = toBytes(data);
    if (f.write(bytes) != bytes.size() || !f.commit()) return fail(IoResult::WriteFailed, f.errorString());
    return {};
}

}  // namespace auc
