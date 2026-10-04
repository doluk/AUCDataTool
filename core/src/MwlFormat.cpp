// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/MwlFormat.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace auc::mwl {

namespace {

IoResult fail(IoResult::Code c, const QString& m) { return {c, m}; }

/// Bounds-checked big-endian reader over a byte buffer.
class Be {
public:
    explicit Be(const QByteArray& b)
        : m_p(reinterpret_cast<const unsigned char*>(b.constData())), m_n(b.size())
    {
    }
    bool have(qsizetype n) const { return m_pos + n <= m_n; }
    quint8 u8() { return m_p[m_pos++]; }
    char ch() { return char(m_p[m_pos++]); }
    quint16 u16() { const quint16 v = qFromBigEndian<quint16>(m_p + m_pos); m_pos += 2; return v; }
    qint16 i16() { return qint16(u16()); }
    quint32 u32() { const quint32 v = qFromBigEndian<quint32>(m_p + m_pos); m_pos += 4; return v; }
    qint32 i32() { return qint32(u32()); }
    float f32()
    {
        const quint32 bits = u32();
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    QByteArray bytes(qsizetype n)
    {
        QByteArray r(reinterpret_cast<const char*>(m_p + m_pos), n);
        m_pos += n;
        return r;
    }
    qsizetype pos() const { return m_pos; }

private:
    const unsigned char* m_p;
    qsizetype m_n;
    qsizetype m_pos = 0;
};

class BeOut {
public:
    void u8(quint8 v) { m_b.append(char(v)); }
    void ch(char c) { m_b.append(c); }
    void u16(quint16 v) { put(v); }
    void i16(qint16 v) { put(quint16(v)); }
    void u32(quint32 v) { put(v); }
    void i32(qint32 v) { put(quint32(v)); }
    void f32(float f)
    {
        quint32 b;
        std::memcpy(&b, &f, 4);
        put(b);
    }
    void raw(const char* p, qsizetype n) { m_b.append(p, n); }
    QByteArray take() { return std::move(m_b); }

private:
    template <typename T>
    void put(T v)
    {
        unsigned char b[sizeof(T)];
        qToBigEndian<T>(v, b);
        m_b.append(reinterpret_cast<const char*>(b), sizeof(T));
    }
    QByteArray m_b;
};

double mwrsScale(const MwrsRunInfo& run)
{
    if (run.version < 1.25) return 0.001;                 // 1.0 – 1.2
    if (run.version < 1.35) return 1.0;                   // 1.3
    return run.takeIntensity ? 1.0 : 0.0001;              // 1.4
}

QString decodeDate(const QByteArray& b)
{
    // day, month, year−1900, hour, minute, second (signed bytes), as in "mwa data field".
    if (b.size() < 6) return {};
    auto v = [&](int i) { return int(qint8(b[i])); };
    const int year = v(2) >= 100 ? 2000 + v(2) % 100 : 1900 + v(2);
    return QStringLiteral("%1.%2.%3 %4:%5:%6")
        .arg(v(0), 2, 10, QLatin1Char('0'))
        .arg(v(1), 2, 10, QLatin1Char('0'))
        .arg(year)
        .arg(v(3), 2, 10, QLatin1Char('0'))
        .arg(v(4), 2, 10, QLatin1Char('0'))
        .arg(v(5), 2, 10, QLatin1Char('0'));
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Run XML
// ---------------------------------------------------------------------------------------

IoResult readMwrsXml(const QString& path, MwrsRunInfo& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    QXmlStreamReader xml(&f);
    MwrsRunInfo info;
    info.fromFile = true;
    QString cell;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto a = xml.attributes();
        const auto name = xml.name();
        if (name == QLatin1String("settings_mwrs_experiment")) {
            bool ok = false;
            const double v = a.value(QLatin1String("version")).toDouble(&ok);
            if (ok) info.version = v;
        } else if (name == QLatin1String("runID")) {
            info.runId = a.value(QLatin1String("name")).toString();
            info.takeIntensity = a.value(QLatin1String("take_intensity")).toString().trimmed().toUpper() != QLatin1String("N");
        } else if (name == QLatin1String("cell")) {
            cell = QString::number(a.value(QLatin1String("id")).toInt());
        } else if (name == QLatin1String("channel")) {
            const QString ch = a.value(QLatin1String("id")).toString().trimmed();
            info.samples.insert(cell + ch, a.value(QLatin1String("sample")).toString().trimmed());
        }
    }
    if (xml.hasError()) return fail(IoResult::NotAucFile, QStringLiteral("XML error: %1").arg(xml.errorString()));
    out = std::move(info);
    return {};
}

IoResult writeMwrsXml(const QString& path, const MwrsRunInfo& info)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(IoResult::CannotOpen, f.errorString());
    QXmlStreamWriter x(&f);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeStartElement(QStringLiteral("settings_mwrs_experiment"));
    x.writeAttribute(QStringLiteral("version"), QString::number(info.version, 'f', 1));
    x.writeStartElement(QStringLiteral("runID"));
    x.writeAttribute(QStringLiteral("name"), info.runId);
    x.writeAttribute(QStringLiteral("take_intensity"), info.takeIntensity ? QStringLiteral("Y") : QStringLiteral("N"));
    QString currentCell;
    for (auto it = info.samples.cbegin(); it != info.samples.cend(); ++it) {
        const QString cell = it.key().left(it.key().size() - 1);
        if (cell != currentCell) {
            if (!currentCell.isEmpty()) x.writeEndElement();
            x.writeStartElement(QStringLiteral("cell"));
            x.writeAttribute(QStringLiteral("id"), cell);
            currentCell = cell;
        }
        x.writeStartElement(QStringLiteral("channel"));
        x.writeAttribute(QStringLiteral("id"), it.key().right(1));
        x.writeAttribute(QStringLiteral("sample"), it.value());
        x.writeEndElement();
    }
    if (!currentCell.isEmpty()) x.writeEndElement();
    x.writeEndElement();  // runID
    x.writeEndElement();  // settings
    x.writeEndDocument();
    if (!f.commit()) return fail(IoResult::WriteFailed, f.errorString());
    return {};
}

// ---------------------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------------------

IoResult parseMwrsHeader(const QByteArray& head, qint64 fileSize, const MwrsRunInfo& run, ScanHeader& out)
{
    Be r(head);
    if (!r.have(24)) return fail(IoResult::NotAucFile, QStringLiteral("file too short for an .mwrs header"));
    ScanHeader h;
    h.cell = r.u8();
    h.channel = r.ch();
    h.scan = r.u16();
    h.scale = mwrsScale(run);
    h.valueBytes = 4;
    h.valueType = 2;

    if (run.version < 1.05) {
        // v1.0: one speed field. Two layouts exist in the wild (see header comment).
        h.rpm = r.u16();
        h.setRpm = 0;
        h.temperature = r.u16() / 10.0;
        h.omega2t = r.f32();
        h.seconds = r.u32();
        h.points = r.u16();
        h.rStart = r.u16() / 1000.0;
        h.rStep = r.u16() / 10000.0;
        const qsizetype afterGrid = r.pos();  // 22

        // Layout A (LabVIEW): nλ i32, λ u32 (nm·10)
        if (r.have(4)) {
            Be a(head);
            for (int i = 0; i < 22; ++i) a.u8();
            const qint32 n = a.i32();
            const qint64 expect = 26 + qint64(n) * 4 + qint64(n) * h.points * 4;
            if (n > 0 && n < 100000 && expect == fileSize && a.have(qsizetype(n) * 4)) {
                for (qint32 i = 0; i < n; ++i) h.wavelengths.push_back(a.u32() / 10.0);
                h.dataOffset = a.pos();
                h.variant = QStringLiteral("mwrs 1.0");
                out = std::move(h);
                return {};
            }
        }
        // Layout B (UltraScan): nλ u16, λ u16 (nm)
        Be b(head);
        for (int i = 0; i < afterGrid; ++i) b.u8();
        const int n = b.u16();
        const qint64 expect = 24 + qint64(n) * 2 + qint64(n) * h.points * 4;
        if (n <= 0 || expect != fileSize || !b.have(qsizetype(n) * 2))
            return fail(IoResult::NotAucFile, QStringLiteral("mwrs 1.0 header does not match the file size"));
        for (int i = 0; i < n; ++i) h.wavelengths.push_back(b.u16());
        h.dataOffset = b.pos();
        h.variant = QStringLiteral("mwrs 1.0");
        out = std::move(h);
        return {};
    }

    if (!r.have(24)) return fail(IoResult::NotAucFile, QStringLiteral("truncated .mwrs header"));
    // LabVIEW: rotor speed first, then the set speed added in v1.1. (UltraScan reads the
    // two in the opposite order; the set speed is the round number.)
    h.rpm = r.u16();
    h.setRpm = r.u16();
    h.temperature = r.u16() / 10.0;
    h.omega2t = r.f32();
    h.seconds = r.u32();
    h.points = r.u16();
    h.rStart = r.u16() / 1000.0;
    h.rStep = r.u16() / 10000.0;
    const int n = r.u16();
    if (n <= 0 || h.points <= 0) return fail(IoResult::NoData, QStringLiteral("no wavelengths or radius points"));
    if (!r.have(qsizetype(n) * 2)) return fail(IoResult::NotAucFile, QStringLiteral("truncated wavelength table"));
    for (int i = 0; i < n; ++i) h.wavelengths.push_back(r.u16());
    h.dataOffset = r.pos();
    const qint64 expect = h.dataOffset + qint64(n) * h.points * 4;
    if (fileSize < expect)
        return fail(IoResult::NotAucFile, QStringLiteral("file incomplete (%1 of %2 bytes)").arg(fileSize).arg(expect));
    h.variant = QStringLiteral("mwrs %1").arg(run.version, 0, 'f', 1);
    out = std::move(h);
    return {};
}

IoResult parseMwHeader(const QByteArray& head, qint64 fileSize, ScanHeader& out)
{
    // Try v1.2 first (it has an explicit value format), accept the layout whose implied
    // size equals the file size.
    {
        Be r(head);
        if (r.have(115)) {
            ScanHeader h;
            r.bytes(4);  // magic
            r.bytes(2);  // version
            h.valueBytes = r.u8();
            h.valueType = r.u8();
            h.darkSubtracted = r.u8() != 0;
            h.dateTime = decodeDate(r.bytes(6));
            r.bytes(6);  // start date/time
            r.u16();     // duration
            r.u16();     // replicates
            h.cell = r.u8();
            h.channel = r.ch();
            h.scan = r.i16();
            h.description = QString::fromLatin1(r.bytes(64)).trimmed().remove(QChar(0));
            h.rpm = r.u16();     // stored as I16 but used as U16 (speeds > 32767 rpm)
            h.setRpm = r.u16();
            h.temperature = r.i16() / 10.0;
            h.omega2t = double(r.u32()) * 10000.0;
            h.seconds = r.i32();
            h.points = r.u16();
            h.rStart = r.u16() / 1000.0;
            const double rEnd = r.u16() / 1000.0;
            const int n = r.u16();
            const int vb = (h.valueBytes == 2) ? 2 : 4;
            const qint64 expect = 115 + qint64(n) * 2 + qint64(n) * vb + qint64(n) * h.points * vb;
            if (n > 0 && h.points > 1 && (h.valueBytes == 2 || h.valueBytes == 4) && expect == fileSize
                && r.have(qsizetype(n) * (2 + vb))) {
                h.valueBytes = vb;
                h.rStep = (rEnd - h.rStart) / (h.points - 1);
                for (int i = 0; i < n; ++i) h.wavelengths.push_back(r.i16() / 10.0);
                h.darkCurrent.resize(size_t(n));
                const QByteArray dark = r.bytes(qsizetype(n) * vb);
                decodeValues(dark.constData(), size_t(n), vb, h.valueType, 1.0, h.darkCurrent.data());
                h.dataOffset = r.pos();
                h.variant = QStringLiteral("mw 1.2");
                out = std::move(h);
                return {};
            }
        }
    }
    Be r(head);
    if (!r.have(100)) return fail(IoResult::NotAucFile, QStringLiteral("file too short for an .mw header"));
    ScanHeader h;
    r.bytes(4);  // magic
    r.bytes(2);  // version
    h.dateTime = decodeDate(r.bytes(6));
    h.cell = r.u8();
    h.channel = r.ch();
    h.scan = r.i16();
    h.description = QString::fromLatin1(r.bytes(64)).trimmed().remove(QChar(0));
    h.rpm = r.u16();  // stored as I16 but used as U16
    h.temperature = r.i16() / 10.0;
    h.omega2t = double(r.u32()) * 10000.0;
    h.seconds = r.i32();
    h.points = r.u16();
    h.rStart = r.u16() / 1000.0;
    const double rEnd = r.u16() / 1000.0;
    const int n = r.u16();
    const qint64 expect = 100 + qint64(n) * 2 + qint64(n) * h.points * 4;
    if (n <= 0 || h.points <= 1 || expect != fileSize || !r.have(qsizetype(n) * 2))
        return fail(IoResult::NotAucFile, QStringLiteral("not a recognised .mw file (size mismatch)"));
    h.rStep = (rEnd - h.rStart) / (h.points - 1);
    for (int i = 0; i < n; ++i) h.wavelengths.push_back(r.i16() / 10.0);
    h.dataOffset = r.pos();
    h.valueBytes = 4;
    h.valueType = 2;
    h.variant = QStringLiteral("mw 1.0");
    out = std::move(h);
    return {};
}

// ---------------------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------------------

void decodeValues(const char* src, std::size_t count, int valueBytes, int valueType, double scale, float* dst)
{
    const auto* p = reinterpret_cast<const unsigned char*>(src);
    const float s = float(scale);
    if (valueBytes == 2) {
        for (std::size_t i = 0; i < count; ++i, p += 2) {
            const quint16 v = qFromBigEndian<quint16>(p);
            dst[i] = (valueType == 1 ? float(v) : float(qint16(v))) * s;
        }
        return;
    }
    for (std::size_t i = 0; i < count; ++i, p += 4) {
        const quint32 v = qFromBigEndian<quint32>(p);
        float x;
        if (valueType == 1)
            x = float(v);
        else if (valueType == 2)
            x = float(qint32(v));
        else
            std::memcpy(&x, &v, 4);
        dst[i] = x * s;
    }
}

// ---------------------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------------------

QByteArray encodeMwrs(const ScanHeader& h, const std::vector<std::vector<std::int32_t>>& rows, double version)
{
    BeOut o;
    o.u8(quint8(h.cell));
    o.ch(h.channel);
    o.u16(quint16(h.scan));
    o.u16(quint16(std::lround(h.rpm)));
    if (version >= 1.05) o.u16(quint16(std::lround(h.setRpm)));
    o.u16(quint16(std::lround(h.temperature * 10.0)));
    o.f32(float(h.omega2t));
    o.u32(quint32(std::lround(h.seconds)));
    o.u16(quint16(h.points));
    o.u16(quint16(std::lround(h.rStart * 1000.0)));
    o.u16(quint16(std::lround(h.rStep * 10000.0)));
    if (version >= 1.05) {
        o.u16(quint16(h.wavelengths.size()));
        for (double wl : h.wavelengths) o.u16(quint16(std::lround(wl)));
    } else {  // LabVIEW v1.0 layout
        o.i32(qint32(h.wavelengths.size()));
        for (double wl : h.wavelengths) o.u32(quint32(std::lround(wl * 10.0)));
    }
    for (const auto& row : rows)
        for (int j = 0; j < h.points; ++j) o.i32(j < int(row.size()) ? row[size_t(j)] : 0);
    return o.take();
}

QByteArray encodeMw12(const ScanHeader& h, const std::vector<std::vector<std::int32_t>>& rows,
                      const std::vector<std::int32_t>& dark)
{
    BeOut o;
    o.raw("MWL1", 4);
    o.u8(1);
    o.u8(2);       // version "12"
    o.u8(4);       // value bytes
    o.u8(2);       // signed
    o.u8(h.darkSubtracted ? 1 : 0);
    const char date[6] = {4, 10, 126, 12, 0, 0};
    o.raw(date, 6);
    o.raw(date, 6);
    o.u16(0);      // duration
    o.u16(1);      // replicates
    o.u8(quint8(h.cell));
    o.ch(h.channel);
    o.i16(qint16(h.scan));
    QByteArray desc = h.description.toLatin1().left(64);
    desc.append(QByteArray(64 - desc.size(), ' '));
    o.raw(desc.constData(), 64);
    o.u16(quint16(std::lround(h.rpm)));
    o.u16(quint16(std::lround(h.setRpm)));
    o.i16(qint16(std::lround(h.temperature * 10.0)));
    o.u32(quint32(std::lround(h.omega2t / 10000.0)));
    o.i32(qint32(std::lround(h.seconds)));
    o.u16(quint16(h.points));
    o.u16(quint16(std::lround(h.rStart * 1000.0)));
    o.u16(quint16(std::lround((h.rStart + h.rStep * (h.points - 1)) * 1000.0)));
    o.u16(quint16(h.wavelengths.size()));
    for (double wl : h.wavelengths) o.i16(qint16(std::lround(wl * 10.0)));
    for (std::size_t i = 0; i < h.wavelengths.size(); ++i) o.i32(i < dark.size() ? dark[i] : 0);
    for (const auto& row : rows)
        for (int j = 0; j < h.points; ++j) o.i32(j < int(row.size()) ? row[size_t(j)] : 0);
    return o.take();
}

}  // namespace auc::mwl
