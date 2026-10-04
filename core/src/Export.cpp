// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Export.h"

#include "auc/XlFormat.h"

#include <QDir>
#include <QSaveFile>
#include <QUuid>

#include <cmath>

namespace auc::exporter {

namespace {

IoResult fail(IoResult::Code c, const QString& msg) { return {c, msg}; }

IoResult saveBytes(const QString& path, const QByteArray& bytes)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(true);  // e.g. Android content:// URIs: no temporary file next to the target
    if (!f.open(QIODevice::WriteOnly)) return fail(IoResult::WriteFailed, QStringLiteral("%1: %2").arg(path, f.errorString()));
    f.write(bytes);
    if (!f.commit()) return fail(IoResult::WriteFailed, QStringLiteral("%1: %2").arg(path, f.errorString()));
    return {};
}

double datasetWavelength(const Dataset& d) { return d.scans.empty() ? 0.0 : d.scans.front().wavelength; }

}  // namespace

QString wavelengthTag(double nm)
{
    const double r = std::round(nm);
    return std::abs(nm - r) < 0.05 ? QString::number(qint64(r)) : QString::number(nm, 'f', 1);
}

IoResult writeBeckman(const Dataset& d, const QString& dir, const std::vector<int>& scanNumbers, QStringList* written)
{
    if (d.scans.empty()) return fail(IoResult::NoData, QStringLiteral("no scans"));
    const QString sub = QStringLiteral("%1%2%3").arg(d.cell).arg(QLatin1Char(d.channel)).arg(wavelengthTag(datasetWavelength(d)));
    const QDir out(QDir(dir).filePath(sub));
    if (!QDir().mkpath(out.path())) return fail(IoResult::WriteFailed, QStringLiteral("cannot create %1").arg(out.path()));
    const QString ext = xl::extension(d.type, d.cell);
    for (std::size_t i = 0; i < d.scans.size(); ++i) {
        const Scan& s = d.scans[i];
        xl::ScanHeader h;
        h.type = d.type;
        h.cell = d.cell;
        h.temperature = s.temperature;
        h.rpm = s.rpm;
        h.seconds = s.seconds;
        h.omega2t = s.omega2t;
        h.wavelength = s.wavelength;
        h.description = QString::fromStdString(d.description);
        const int number = i < scanNumbers.size() ? scanNumbers[i] : int(i) + 1;
        const QString name = QStringLiteral("%1%2.%3").arg(QLatin1Char(d.channel)).arg(number, 5, 10, QLatin1Char('0')).arg(ext);
        const QString path = out.filePath(name);
        if (IoResult r = saveBytes(path, xl::format(h, d.radius, s.values, s.stddev)); !r.ok()) return r;
        if (written) *written << path;
    }
    return {};
}

IoResult writeUs3(const Dataset& d, const QString& dir, const QString& runId, QString* written)
{
    if (d.scans.empty()) return fail(IoResult::NoData, QStringLiteral("no scans"));
    if (!QDir().mkpath(dir)) return fail(IoResult::WriteFailed, QStringLiteral("cannot create %1").arg(dir));
    Dataset out = d;
    if (out.guid == std::array<std::uint8_t, 16>{}) {
        const QByteArray g = QUuid::createUuid().toRfc4122();
        std::copy(g.begin(), g.begin() + 16, out.guid.begin());
    }
    const std::size_t np = out.radius.size();
    const double dr = np > 1 ? (out.radius.back() - out.radius.front()) / double(np - 1) : 0.0;
    for (Scan& s : out.scans) {
        s.deltaR = dr;
        s.interpolated.assign((np + 7) / 8, 0);
        for (std::size_t j = 0; j < s.values.size(); ++j) {
            if (std::isfinite(s.values[j])) continue;
            s.values[j] = 0.f;
            s.interpolated[j / 8] |= std::uint8_t(0x80u >> (j % 8));
        }
        for (float& x : s.stddev)
            if (!std::isfinite(x)) x = 0.f;
    }
    QString safeRun = runId;
    safeRun.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString name = QStringLiteral("%1.%2.%3.%4.%5.auc")
                             .arg(safeRun, QString::fromStdString(toCode(out.type)))
                             .arg(out.cell)
                             .arg(QLatin1Char(out.channel))
                             .arg(wavelengthTag(datasetWavelength(out)));
    const QString path = QDir(dir).filePath(name);
    if (IoResult r = AucFile::write(path, out); !r.ok()) return r;
    if (written) *written = path;
    return {};
}

IoResult writeOrigin(const Dataset& d, const QString& path, const OriginColumns& cols)
{
    if (d.scans.empty()) return fail(IoResult::NoData, QStringLiteral("no scans"));
    QByteArray b;
    b.reserve(qsizetype(d.radius.size() * (d.scans.size() + 1) * 12 + 1024));
    auto row = [&](const QString& first, auto&& cell) {
        b += first.toUtf8();
        for (std::size_t i = 0; i < d.scans.size(); ++i) {
            b += '\t';
            b += cell(i).toUtf8();
        }
        b += "\r\n";
    };
    row(cols.xName, [&](std::size_t) { return cols.yName; });
    row(cols.xUnit, [&](std::size_t) { return cols.yUnit; });
    row(QString(), [&](std::size_t i) {
        if (i < cols.comments.size()) return cols.comments[i];
        return QStringLiteral("t = %1 s").arg(d.scans[i].seconds, 0, 'f', 0);
    });
    for (std::size_t j = 0; j < d.radius.size(); ++j) {
        b += QByteArray::number(d.radius[j], 'f', 5);
        for (const Scan& s : d.scans) {
            b += '\t';
            const float v = j < s.values.size() ? s.values[j] : NAN;
            b += std::isfinite(v) ? QByteArray::number(double(v), 'g', 7) : QByteArray("--");
        }
        b += "\r\n";
    }
    return saveBytes(path, b);
}

}  // namespace auc::exporter
