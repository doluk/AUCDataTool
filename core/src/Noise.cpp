// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Noise.h"

#include "auc/Processing.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

namespace auc::noise {

namespace {

IoResult fail(IoResult::Code c, const QString& msg) { return {c, msg}; }

QString typeName(Type t) { return t == Type::TimeInvariant ? QStringLiteral("TI") : QStringLiteral("RI"); }

IoResult readXml(const QByteArray& bytes, Type expected, NoiseVector& out)
{
    QXmlStreamReader xml(bytes);
    NoiseVector n;
    bool sawNoise = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const auto a = xml.attributes();
        if (xml.name() == QLatin1String("noise")) {
            if (sawNoise) break;  // files hold one vector; ignore anything after it
            sawNoise = true;
            const QString t = a.value(QLatin1String("type")).toString().trimmed().toLower();
            if (t == QLatin1String("ti"))
                n.type = Type::TimeInvariant;
            else if (t == QLatin1String("ri"))
                n.type = Type::RadiallyInvariant;
            else
                return fail(IoResult::BadType, QStringLiteral("unknown noise type '%1'").arg(t));
            n.description = a.value(QLatin1String("description")).toString();
            n.noiseGuid = a.value(QLatin1String("noiseGUID")).toString();
            n.modelGuid = a.value(QLatin1String("modelGUID")).toString();
            n.minRadius = a.value(QLatin1String("minradius")).toDouble();
            n.maxRadius = a.value(QLatin1String("maxradius")).toDouble();
        } else if (xml.name() == QLatin1String("d") && sawNoise) {
            bool ok = false;
            const double v = a.value(QLatin1String("v")).toDouble(&ok);
            if (!ok) return fail(IoResult::NotAucFile, QStringLiteral("invalid value at line %1").arg(xml.lineNumber()));
            n.values.push_back(v);
        }
    }
    if (xml.hasError() && !sawNoise)
        return fail(IoResult::NotAucFile, QStringLiteral("XML error: %1").arg(xml.errorString()));
    if (!sawNoise) return fail(IoResult::NotAucFile, QStringLiteral("no <noise> element"));
    if (n.values.empty()) return fail(IoResult::NoData, QStringLiteral("noise file contains no values"));
    if (n.type != expected)
        return fail(IoResult::BadType, QStringLiteral("file contains %1 noise, expected %2")
                                            .arg(typeName(n.type), typeName(expected)));
    out = std::move(n);
    return {};
}

IoResult readText(const QByteArray& bytes, Type expected, NoiseVector& out)
{
    NoiseVector n;
    n.type = expected;
    std::vector<double> first;
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    int lineNo = 0;
    int columns = -1;
    for (const QByteArray& raw : bytes.split('\n')) {
        ++lineNo;
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        QList<double> nums;
        for (const QString& p : parts) {
            bool ok = false;
            const double v = p.toDouble(&ok);
            if (!ok) break;
            nums << v;
        }
        if (nums.size() != parts.size()) {
            if (first.empty() && n.values.empty()) continue;  // header line
            return fail(IoResult::NotAucFile, QStringLiteral("line %1 is not numeric").arg(lineNo));
        }
        if (columns < 0) columns = int(nums.size());
        if (nums.size() != columns)
            return fail(IoResult::NotAucFile, QStringLiteral("line %1 has %2 columns, expected %3")
                                                  .arg(lineNo).arg(nums.size()).arg(columns));
        if (columns == 1) {
            n.values.push_back(nums[0]);
        } else {
            first.push_back(nums[0]);
            n.values.push_back(nums[1]);
        }
    }
    if (n.values.empty()) return fail(IoResult::NoData, QStringLiteral("no values found"));
    if (expected == Type::TimeInvariant && !first.empty()) {
        n.minRadius = first.front();
        n.maxRadius = first.back();
    }
    out = std::move(n);
    return {};
}

}  // namespace

IoResult readNoiseFile(const QString& path, Type expected, NoiseVector& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(IoResult::CannotOpen, f.errorString());
    const QByteArray bytes = f.readAll();
    const QByteArray head = bytes.left(256).trimmed();
    if (head.startsWith("<?xml") || head.startsWith("<NoiseData") || head.startsWith("<!DOCTYPE"))
        return readXml(bytes, expected, out);
    return readText(bytes, expected, out);
}

IoResult writeNoiseFile(const QString& path, const NoiseVector& n)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(IoResult::CannotOpen, f.errorString());
    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE US_Noise>"));
    xml.writeStartElement(QStringLiteral("NoiseData"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("noise"));
    const bool ti = n.type == Type::TimeInvariant;
    xml.writeAttribute(QStringLiteral("type"), ti ? QStringLiteral("ti") : QStringLiteral("ri"));
    xml.writeAttribute(QStringLiteral("description"), n.description);
    xml.writeAttribute(QStringLiteral("modelGUID"), n.modelGuid);
    xml.writeAttribute(QStringLiteral("noiseGUID"), n.noiseGuid);
    if (ti) {
        xml.writeAttribute(QStringLiteral("minradius"), QString::number(n.minRadius, 'g', 10));
        xml.writeAttribute(QStringLiteral("maxradius"), QString::number(n.maxRadius, 'g', 10));
    }
    for (double v : n.values) {
        xml.writeStartElement(QStringLiteral("d"));
        xml.writeAttribute(QStringLiteral("v"), QString::number(v, 'g', 10));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    if (!f.commit()) return fail(IoResult::WriteFailed, f.errorString());
    return {};
}

QString apply(Dataset& d, const NoiseVector& n, bool remove)
{
    const double sign = remove ? -1.0 : 1.0;
    const std::size_t count = n.values.size();
    if (count == 0) return QStringLiteral("noise vector is empty");

    if (n.type == Type::RadiallyInvariant) {
        if (count != d.scanCount())
            return QStringLiteral("RI noise has %1 values but the data has %2 scans").arg(count).arg(d.scanCount());
        for (std::size_t i = 0; i < d.scans.size(); ++i) {
            const float v = float(sign * n.values[i]);
            for (float& x : d.scans[i].values) x += v;
        }
        return {};
    }

    // Time-invariant: locate the covered radius range.
    const std::size_t np = d.pointCount();
    std::size_t start = 0;
    if (count != np) {
        if (n.minRadius <= 0.0 || np < 2)
            return QStringLiteral("TI noise has %1 values but the data has %2 points, and the file gives no radius range")
                .arg(count).arg(np);
        start = proc::nearestIndex(d.radius, n.minRadius);
        if (start + count > np)
            return QStringLiteral("TI noise (%1 values from r = %2 cm) extends beyond the data")
                .arg(count).arg(n.minRadius);
        const double step = (d.radius.back() - d.radius.front()) / double(np - 1);
        if (std::abs(d.radius[start] - n.minRadius) > 0.5 * step
            || (n.maxRadius > 0.0 && std::abs(d.radius[start + count - 1] - n.maxRadius) > 0.5 * step))
            return QStringLiteral("TI noise range %1–%2 cm does not match the radius grid of the data")
                .arg(n.minRadius).arg(n.maxRadius);
    }
    for (auto& s : d.scans) {
        for (std::size_t k = 0; k < count && start + k < s.values.size(); ++k)
            s.values[start + k] += float(sign * n.values[k]);
    }
    return {};
}

}  // namespace auc::noise
