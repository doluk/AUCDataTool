// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Channel.h"
#include "auc/Export.h"
#include "auc/XlFormat.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace auc;

namespace {

void writeText(const QString& path, const QByteArray& b)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(b);
}

/// One intensity scan with sample = 1000 − 100·i·(r − 6) and reference = 2000 on a
/// slightly jittered radial grid (as in real XL files).
QByteArray intensityScan(int cell, int scan, double wavelength, double jitter)
{
    xl::ScanHeader h;
    h.type = DataType::RadialIntensity;
    h.cell = cell;
    h.temperature = 20.0;
    h.rpm = 50000;
    h.seconds = 300.0 * scan;
    h.omega2t = 8.2e9 * scan;
    h.wavelength = wavelength;
    h.description = QStringLiteral("Test sample");
    std::vector<double> x;
    std::vector<float> s, r;
    for (int j = 0; j < 101; ++j) {
        const double rr = 6.0 + 0.01 * j + (j > 0 && j < 100 ? jitter * ((j % 3) - 1) : 0.0);
        x.push_back(rr);
        s.push_back(float(1000.0 - 100.0 * scan * (rr - 6.0)));
        r.push_back(2000.f);
    }
    return xl::format(h, x, s, r);
}

}  // namespace

class TestXl : public QObject {
    Q_OBJECT
private slots:
    void suffixes()
    {
        QVERIFY(xl::isXlSuffix(QStringLiteral("RA1")));
        QVERIFY(xl::isXlSuffix(QStringLiteral("ri2")));
        QVERIFY(xl::isXlSuffix(QStringLiteral("IP8")));
        QVERIFY(!xl::isXlSuffix(QStringLiteral("RA")));
        QVERIFY(!xl::isXlSuffix(QStringLiteral("auc")));
        QVERIFY(!xl::isXlSuffix(QStringLiteral("mw3")));
    }

    void parseClassicHeader()
    {
        // XL-A style with four-digit exponents.
        const QByteArray b = "Simulation\r\nR 1 20.0 45000 0000099 7.4396E08   0 3\r\n"
                             "   5.7700  1.00504E-0001   0.00000E+0000\r\n"
                             "   5.7710  1.10000E-0001   2.00000E-0003\r\n";
        xl::ScanFile f;
        QVERIFY(xl::parse(b, QStringLiteral("RA1"), f).ok());
        QCOMPARE(f.header.type, DataType::RadialAbsorbance);
        QCOMPARE(f.header.cell, 1);
        QCOMPARE(f.header.rpm, 45000.0);
        QCOMPARE(f.header.seconds, 99.0);
        QCOMPARE(f.header.omega2t, 7.4396e8);
        QCOMPARE(f.header.replicates, 3);
        QCOMPARE(f.x.size(), std::size_t(2));
        QCOMPARE(f.value[0], 0.100504f);
        QCOMPARE(f.third[1], 0.002f);
    }

    void parseInterference()
    {
        const QByteArray b = "cm/pixel:  0.00070460705, BFE-DGE CsCl\nP 2 20.0 48000 0000375 7.5011E+09 660 1\n"
                             "   5.8042    2.41283E+00\n   5.8049    5.03081E-01\n";
        xl::ScanFile f;
        QVERIFY(xl::parse(b, QStringLiteral("ip2"), f).ok());
        QCOMPARE(f.header.type, DataType::Interference);
        QCOMPARE(f.header.description, QStringLiteral("BFE-DGE CsCl"));
        QVERIFY(f.third.empty());
        QCOMPARE(f.value[1], 0.503081f);
    }

    void typeFromHeaderLetter()
    {
        // The header letter decides, not the extension (".RI6" holding absorbance).
        const QByteArray b = "x\nR 6 20.0 50000 0035153 9.6078E11 220 3\n 5.75 0.97 0\n";
        xl::ScanHeader h;
        QVERIFY(xl::parseHeader(b, QStringLiteral("RI6"), h).ok());
        QCOMPARE(h.type, DataType::RadialAbsorbance);
        QVERIFY(xl::parseHeader("x\nW 1 20.0 0 0000000 0.0E+00 6.500 1\n 250.0 0.5 0\n", QStringLiteral("WI1"), h).ok());
        QCOMPARE(h.type, DataType::WavelengthIntensity);
        QCOMPARE(h.wavelength, 6.5);
    }

    void formatRoundTrip()
    {
        xl::ScanHeader h;
        h.type = DataType::RadialAbsorbance;
        h.cell = 3;
        h.temperature = 19.9;
        h.rpm = 48000;
        h.seconds = 393;
        h.omega2t = 7.9561e9;
        h.wavelength = 280;
        h.description = QStringLiteral("Sample");
        const std::vector<double> x{5.8, 5.801, 5.802};
        const std::vector<float> v{0.1f, 0.2f, std::nanf("")};
        xl::ScanFile f;
        QVERIFY(xl::parse(xl::format(h, x, v), QStringLiteral("RA3"), f).ok());
        QCOMPARE(f.header.cell, 3);
        QCOMPARE(f.header.wavelength, 280.0);
        QCOMPARE(f.header.seconds, 393.0);
        QCOMPARE(f.x.size(), std::size_t(2));  // NaN points are not written
        QCOMPARE(f.value[1], 0.2f);
        QCOMPARE(xl::extension(DataType::RadialIntensity, 2), QStringLiteral("RI2"));
    }

    void resampleOntoGrid()
    {
        const std::vector<double> x{1.0, 2.0, 4.0};
        const std::vector<float> y{10.f, 20.f, 40.f};
        const std::vector<double> g{0.5, 1.0, 3.0, 4.0, 4.5};
        float out[5];
        xl::resample(x, y, g, out);
        QVERIFY(std::isnan(out[0]));
        QCOMPARE(out[1], 10.f);
        QCOMPARE(out[2], 30.f);
        QCOMPARE(out[3], 40.f);
        QVERIFY(std::isnan(out[4]));
    }

    void channelsFromFolders()
    {
        // Run folder with wavelength folders "2A280" and "2A230"; intensity files carry
        // sample and reference intensity → channels 2A and 2B, two wavelengths each.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (int scan = 1; scan <= 3; ++scan) {
            writeText(dir.filePath(QStringLiteral("2A280/A%1.RI2").arg(scan, 5, 10, QLatin1Char('0'))),
                      intensityScan(2, scan, 280, 0.0004));
            writeText(dir.filePath(QStringLiteral("2A230/A%1.RI2").arg(scan, 5, 10, QLatin1Char('0'))),
                      intensityScan(2, scan, 230, 0.0));
        }
        const OpenResult res = openData({dir.path()});
        QVERIFY2(res.warnings.isEmpty(), qPrintable(res.warnings.join('\n')));
        QCOMPARE(res.channels.size(), std::size_t(2));
        const auto& a = *res.channels[0];
        const auto& b = *res.channels[1];
        QCOMPARE(a.key(), QStringLiteral("2A"));
        QCOMPARE(b.key(), QStringLiteral("2B"));
        QCOMPARE(a.format, SourceFormat::Xl);
        QVERIFY(!a.absorbanceData);
        QCOMPARE(a.wavelengths, (std::vector<double>{230, 280}));
        QCOMPARE(a.scans.size(), std::size_t(3));
        QCOMPARE(a.scans[2].number, 3);
        QCOMPARE(a.scans[1].seconds, 600.0);
        QCOMPARE(a.radius.size(), std::size_t(101));
        QCOMPARE(a.runId, QDir(dir.path()).dirName());

        std::shared_ptr<const Dataset> d;
        QVERIFY(a.wavelengthSlice(1, d).ok());
        // Linear in r, so interpolation on the jittered grid is exact.
        for (std::size_t j = 0; j < a.radius.size(); j += 10)
            QVERIFY(std::abs(d->scans[2].values[j] - float(1000.0 - 300.0 * (a.radius[j] - 6.0))) < 0.05f);
        QVERIFY(b.wavelengthSlice(0, d).ok());
        QCOMPARE(d->scans[0].values[50], 2000.f);

        // Spectra and full-scan access
        std::vector<float> m;
        QVERIFY(a.scanMatrix(0, m).ok());
        QCOMPARE(m.size(), std::size_t(2 * 101));
        QVERIFY(a.spectra(50, 1, d).ok());
        QCOMPARE(d->radius, a.wavelengths);
        QCOMPARE(d->scans.size(), std::size_t(3));
        QVERIFY(std::abs(d->scans[0].values[0] - float(1000.0 - 100.0 * (a.radius[50] - 6.0))) < 0.05f);
    }

    void exportRoundTrips()
    {
        Dataset d;
        d.type = DataType::RadialAbsorbance;
        d.cell = 3;
        d.channel = 'B';
        d.description = "exported";
        for (int j = 0; j < 50; ++j) d.radius.push_back(6.0 + 0.002 * j);
        for (int i = 0; i < 4; ++i) {
            Scan s;
            s.temperature = 20.0;
            s.rpm = 42000;
            s.seconds = 100.0 * (i + 1);
            s.omega2t = 1.9e9 * (i + 1);
            s.wavelength = 280;
            for (int j = 0; j < 50; ++j) s.values.push_back(0.01f * float(i * 50 + j));
            d.scans.push_back(s);
        }
        d.scans[1].values[7] = std::nanf("");
        QTemporaryDir dir;

        // Beckman: re-opened as an XL channel with the same values.
        QStringList files;
        QVERIFY(exporter::writeBeckman(d, dir.filePath("xl"), {5, 6, 7, 8}, &files).ok());
        QCOMPARE(files.size(), 4);
        QVERIFY(files[0].endsWith("xl/3B280/B00005.RA3"));
        OpenResult res = openData({dir.filePath("xl")});
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = *res.channels[0];
        QCOMPARE(c.key(), QStringLiteral("3B"));
        QVERIFY(c.absorbanceData);
        QCOMPARE(c.scans[3].number, 8);
        std::shared_ptr<const Dataset> back;
        QVERIFY(c.wavelengthSlice(0, back).ok());
        QCOMPARE(back->scans.size(), std::size_t(4));
        QVERIFY(std::abs(back->scans[2].values[10] - d.scans[2].values[10]) < 1e-4f);
        QVERIFY(std::abs(back->scans[1].values[7] - 0.5f * (d.scans[1].values[6] + d.scans[1].values[8])) < 1e-4f);

        // US3 openAUC
        QString path;
        QVERIFY(exporter::writeUs3(d, dir.filePath("us3"), "run.1", &path).ok());
        QVERIFY(path.endsWith("us3/run_1.RA.3.B.280.auc"));
        Dataset a;
        QVERIFY(AucFile::read(path, a).ok());
        QCOMPARE(a.scans.size(), std::size_t(4));
        QVERIFY(a.scans[1].isInterpolated(7));
        QVERIFY(!a.scans[1].isInterpolated(6));
        QVERIFY(std::abs(a.scans[3].values[49] - d.scans[3].values[49]) < 1e-3f);

        // Origin
        QVERIFY(exporter::writeOrigin(d, dir.filePath("o.dat"), {"Radius", "cm", "Absorbance", "OD", {}}).ok());
        QFile f(dir.filePath("o.dat"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = f.readAll().split('\n');
        QCOMPARE(lines.size(), 3 + 50 + 1);
        QVERIFY(lines[0].startsWith("Radius\tAbsorbance\t"));
        QVERIFY(lines[2].startsWith("\tt = 100 s\tt = 200 s"));
        QCOMPARE(lines[3 + 7].split('\t')[2].trimmed(), QByteArray("--"));
    }
};

QTEST_MAIN(TestXl)
#include "tst_xl.moc"
