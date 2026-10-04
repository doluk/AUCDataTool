// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/XlFormat.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <limits>

namespace auc::xl {

namespace {

IoResult fail(IoResult::Code c, const QString& msg) { return {c, msg}; }

/// Splits a line into whitespace-separated tokens (views into `line`).
int tokens(QByteArrayView line, QByteArrayView* out, int max)
{
    int n = 0;
    qsizetype i = 0;
    const qsizetype len = line.size();
    while (i < len && n < max) {
        while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == ',')) ++i;
        const qsizetype s = i;
        while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != ',') ++i;
        if (i > s) out[n++] = line.sliced(s, i - s);
    }
    return n;
}

/// Next line of `b` starting at `pos` (without the line break); advances `pos`.
bool nextLine(QByteArrayView b, qsizetype& pos, QByteArrayView& line)
{
    if (pos >= b.size()) return false;
    qsizetype e = b.indexOf('\n', pos);
    if (e < 0) e = b.size();
    line = b.sliced(pos, e - pos);
    if (line.endsWith('\r')) line.chop(1);
    pos = e + 1;
    return true;
}

bool typeFromLetter(char letter, const QString& suffix, DataType& t)
{
    const QString code = suffix.left(2).toUpper();
    switch (letter) {
    case 'R': t = DataType::RadialAbsorbance; return true;
    case 'I': t = DataType::RadialIntensity; return true;
    case 'P': t = DataType::Interference; return true;
    case 'F': t = DataType::Fluorescence; return true;
    case 'W': t = code == QLatin1String("WI") ? DataType::WavelengthIntensity : DataType::WavelengthAbsorbance; return true;
    default: return fromCode(code.toStdString(), t);
    }
}

}  // namespace

bool isXlSuffix(const QString& suffix)
{
    if (suffix.size() != 3 || !suffix[2].isDigit()) return false;
    const QString c = suffix.left(2).toUpper();
    return c == QLatin1String("RA") || c == QLatin1String("RI") || c == QLatin1String("IP") || c == QLatin1String("WA")
        || c == QLatin1String("WI") || c == QLatin1String("FI");
}

QStringList nameFilters()
{
    return {QStringLiteral("*.ra?"), QStringLiteral("*.ri?"), QStringLiteral("*.ip?"),
            QStringLiteral("*.wa?"), QStringLiteral("*.wi?"), QStringLiteral("*.fi?")};
}

IoResult parseHeader(const QByteArray& head, const QString& suffix, ScanHeader& out)
{
    const QByteArrayView b(head);
    qsizetype pos = 0;
    QByteArrayView l1, l2, l3;
    if (!nextLine(b, pos, l1) || !nextLine(b, pos, l2)) return fail(IoResult::NotAucFile, QStringLiteral("missing XL header lines"));
    QByteArrayView t[10];
    const int n = tokens(l2, t, 10);
    if (n < 7 || t[0].size() != 1) return fail(IoResult::NotAucFile, QStringLiteral("not an XL scan header"));
    ScanHeader h;
    h.letter = char(t[0][0]);
    if (!typeFromLetter(h.letter, suffix, h.type)) return fail(IoResult::BadType, QStringLiteral("unknown scan type '%1'").arg(QLatin1Char(h.letter)));
    bool ok[7];
    h.cell = t[1].toInt(&ok[0]);
    h.temperature = t[2].toDouble(&ok[1]);
    h.rpm = t[3].toDouble(&ok[2]);
    h.seconds = t[4].toDouble(&ok[3]);
    h.omega2t = t[5].toDouble(&ok[4]);
    h.wavelength = t[6].toDouble(&ok[5]);
    h.replicates = n > 7 ? t[7].toInt(&ok[6]) : 1;
    for (int i = 0; i < 6; ++i)
        if (!ok[i]) return fail(IoResult::NotAucFile, QStringLiteral("unreadable XL scan header"));

    QString desc = QString::fromLatin1(l1).trimmed();
    if (desc.startsWith(QLatin1String("cm/pixel:"), Qt::CaseInsensitive)) desc = desc.section(QLatin1Char(','), 1).trimmed();
    h.description = desc;

    // Column count from the first data line.
    if (nextLine(b, pos, l3)) {
        QByteArrayView c[4];
        h.columns = tokens(l3, c, 4);
    }
    if (h.columns < 2) return fail(IoResult::NoData, QStringLiteral("no readings"));
    out = std::move(h);
    return {};
}

IoResult readHeader(const QString& path, ScanHeader& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    return parseHeader(f.read(1024), QFileInfo(path).suffix(), out);
}

IoResult parse(const QByteArray& bytes, const QString& suffix, ScanFile& out)
{
    ScanFile s;
    if (IoResult r = parseHeader(bytes, suffix, s.header); !r.ok()) return r;
    const QByteArrayView b(bytes);
    qsizetype pos = 0;
    QByteArrayView line;
    nextLine(b, pos, line);
    nextLine(b, pos, line);
    const bool three = s.header.columns >= 3;
    const qsizetype estimate = bytes.size() / 40 + 4;
    s.x.reserve(size_t(estimate));
    s.value.reserve(size_t(estimate));
    if (three) s.third.reserve(size_t(estimate));
    QByteArrayView t[4];
    while (nextLine(b, pos, line)) {
        const int n = tokens(line, t, 4);
        if (n < 2) continue;  // blank line / trailer
        bool ok1 = false, ok2 = false;
        const double x = t[0].toDouble(&ok1);
        const double v = t[1].toDouble(&ok2);
        if (!ok1 || !ok2) continue;
        s.x.push_back(x);
        s.value.push_back(float(v));
        if (three) s.third.push_back(n > 2 ? float(t[2].toDouble()) : 0.f);
    }
    if (s.x.empty()) return fail(IoResult::NoData, QStringLiteral("no readings"));
    out = std::move(s);
    return {};
}

IoResult read(const QString& path, ScanFile& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    return parse(f.readAll(), QFileInfo(path).suffix(), out);
}

QString extension(DataType t, int cell) { return QString::fromStdString(toCode(t)) + QString::number(cell); }

QByteArray format(const ScanHeader& h, std::span<const double> x, std::span<const float> value, std::span<const float> third)
{
    QByteArray out;
    out.reserve(qsizetype(x.size()) * 42 + 200);
    out += h.description.toLatin1().replace('\n', ' ');
    out += "\r\n";
    char letter = h.letter;
    switch (h.type) {
    case DataType::RadialAbsorbance: letter = 'R'; break;
    case DataType::RadialIntensity: letter = 'I'; break;
    case DataType::Interference: letter = 'P'; break;
    case DataType::Fluorescence: letter = 'F'; break;
    case DataType::WavelengthAbsorbance:
    case DataType::WavelengthIntensity: letter = 'W'; break;
    }
    char buf[160];
    const bool wavelengthScan = h.type == DataType::WavelengthAbsorbance || h.type == DataType::WavelengthIntensity;
    const QByteArray wl = wavelengthScan ? QByteArray::number(h.wavelength, 'f', 3) : QByteArray::number(qRound(h.wavelength));
    std::snprintf(buf, sizeof buf, "%c %d %.1f %d %07d %.4E %s %d\r\n", letter, h.cell, h.temperature, int(std::lround(h.rpm)),
                  int(std::lround(h.seconds)), h.omega2t, wl.constData(), h.replicates);
    out += buf;
    const bool interference = h.type == DataType::Interference;
    for (std::size_t j = 0; j < x.size() && j < value.size(); ++j) {
        if (!std::isfinite(value[j])) continue;
        if (interference)
            std::snprintf(buf, sizeof buf, "%9.4f %14.5E\r\n", x[j], double(value[j]));
        else
            std::snprintf(buf, sizeof buf, "%9.4f %14.5E %14.5E\r\n", x[j], double(value[j]),
                          j < third.size() ? double(third[j]) : 0.0);
        out += buf;
    }
    return out;
}

void resample(std::span<const double> x, std::span<const float> y, std::span<const double> grid, float* out)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::size_t n = std::min(x.size(), y.size());
    if (n == 0) {
        std::fill(out, out + grid.size(), nan);
        return;
    }
    std::size_t k = 0;
    // Tolerance: half a percent of a step, so identical grids reproduce the values exactly.
    const double tol = n > 1 ? 0.005 * (x[n - 1] - x[0]) / double(n - 1) : 1e-9;
    for (std::size_t j = 0; j < grid.size(); ++j) {
        const double g = grid[j];
        if (g < x[0] - tol || g > x[n - 1] + tol) {
            out[j] = nan;
            continue;
        }
        while (k + 1 < n && x[k + 1] < g) ++k;
        if (std::abs(x[k] - g) <= tol || k + 1 >= n) {
            out[j] = y[k];
        } else if (std::abs(x[k + 1] - g) <= tol) {
            out[j] = y[k + 1];
        } else {
            const double f = (g - x[k]) / (x[k + 1] - x[k]);
            out[j] = float(y[k] + f * (y[k + 1] - y[k]));
        }
    }
}

}  // namespace auc::xl
