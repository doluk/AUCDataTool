// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Noise.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace auc;
using namespace auc::noise;

namespace {

Dataset grid(int scans, int points)
{
    Dataset d;
    for (int j = 0; j < points; ++j) d.radius.push_back(6.0 + 0.01 * j);
    for (int i = 0; i < scans; ++i) {
        Scan s;
        s.values.assign(size_t(points), 1.0f);
        d.scans.push_back(s);
    }
    return d;
}

void writeText(const QString& path, const QByteArray& text)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

}  // namespace

class TestNoise : public QObject {
    Q_OBJECT
private slots:
    void xmlRoundTrip()
    {
        QTemporaryDir dir;
        NoiseVector n;
        n.type = Type::TimeInvariant;
        n.minRadius = 6.02;
        n.maxRadius = 6.05;
        n.values = {0.1, -0.2, 0.3, 0.05};
        n.description = "test";
        QVERIFY(writeNoiseFile(dir.filePath("ti.xml"), n).ok());

        NoiseVector back;
        const IoResult res = readNoiseFile(dir.filePath("ti.xml"), Type::TimeInvariant, back);
        QVERIFY2(res.ok(), qPrintable(res.message));
        QCOMPARE(back.values.size(), std::size_t(4));
        QCOMPARE(back.values[1], -0.2);
        QCOMPARE(back.minRadius, 6.02);
        QCOMPARE(back.description, QString("test"));

        // Declared type must match the requested one.
        QCOMPARE(readNoiseFile(dir.filePath("ti.xml"), Type::RadiallyInvariant, back).code, IoResult::BadType);
    }

    void readsUltraScanXml()
    {
        // As written by UltraScan III (US_Noise::write).
        QTemporaryDir dir;
        writeText(dir.filePath("us3.xml"),
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE US_Noise>\n"
                  "<NoiseData version=\"1.0\">\n"
                  " <noise type=\"ri\" description=\"demo-run.2A280.2DSA-IT\" modelGUID=\"m\" noiseGUID=\"n\">\n"
                  "  <d v=\"0.001\"/>\n  <d v=\"-0.002\"/>\n  <d v=\"0.0005\"/>\n"
                  " </noise>\n</NoiseData>\n");
        NoiseVector n;
        QVERIFY(readNoiseFile(dir.filePath("us3.xml"), Type::RadiallyInvariant, n).ok());
        QCOMPARE(n.values.size(), std::size_t(3));
        QCOMPARE(n.values[1], -0.002);
    }

    void readsPlainText()
    {
        QTemporaryDir dir;
        writeText(dir.filePath("one.txt"), "# RI noise\n0.1\n0.2\n\n0.3\n");
        NoiseVector n;
        QVERIFY(readNoiseFile(dir.filePath("one.txt"), Type::RadiallyInvariant, n).ok());
        QCOMPARE(n.values.size(), std::size_t(3));

        writeText(dir.filePath("two.csv"), "radius,noise\n6.01,0.5\n6.02;0.6\n6.03\t0.7\n");
        QVERIFY(readNoiseFile(dir.filePath("two.csv"), Type::TimeInvariant, n).ok());
        QCOMPARE(n.minRadius, 6.01);
        QCOMPARE(n.maxRadius, 6.03);
        QCOMPARE(n.values[2], 0.7);

        writeText(dir.filePath("bad.txt"), "0.1\nabc\n");
        QCOMPARE(readNoiseFile(dir.filePath("bad.txt"), Type::TimeInvariant, n).code, IoResult::NotAucFile);
    }

    void appliesTiOverFullRange()
    {
        Dataset d = grid(3, 5);
        NoiseVector n;
        n.values = {0.1, 0.2, 0.3, 0.4, 0.5};
        QVERIFY(apply(d, n).isEmpty());
        QVERIFY(std::abs(d.scans[2].values[4] - 0.5f) < 1e-6);
        QVERIFY(apply(d, n, false).isEmpty());  // add back
        QVERIFY(std::abs(d.scans[2].values[4] - 1.0f) < 1e-6);
    }

    void appliesTiOverEditedRange()
    {
        Dataset d = grid(2, 10);  // r = 6.00 … 6.09
        NoiseVector n;
        n.minRadius = 6.03;
        n.maxRadius = 6.05;
        n.values = {0.5, 0.5, 0.5};
        QVERIFY(apply(d, n).isEmpty());
        QCOMPARE(d.scans[0].values[2], 1.0f);
        QCOMPARE(d.scans[0].values[3], 0.5f);
        QCOMPARE(d.scans[0].values[5], 0.5f);
        QCOMPARE(d.scans[0].values[6], 1.0f);
    }

    void rejectsMismatch()
    {
        Dataset d = grid(4, 10);
        NoiseVector ri;
        ri.type = Type::RadiallyInvariant;
        ri.values = {1, 2, 3};  // 3 ≠ 4 scans
        QVERIFY(!apply(d, ri).isEmpty());

        NoiseVector ti;
        ti.values = {1, 2, 3};  // no radius range and 3 ≠ 10 points
        QVERIFY(!apply(d, ti).isEmpty());

        ti.minRadius = 6.08;  // would run past the last point
        ti.maxRadius = 6.10;
        QVERIFY(!apply(d, ti).isEmpty());

        ti.minRadius = 6.02;
        ti.maxRadius = 6.30;  // range inconsistent with grid
        QVERIFY(!apply(d, ti).isEmpty());
        QCOMPARE(d.scans[0].values[2], 1.0f);  // nothing changed on failure
    }

    void appliesRi()
    {
        Dataset d = grid(2, 3);
        NoiseVector ri;
        ri.type = Type::RadiallyInvariant;
        ri.values = {0.25, -0.25};
        QVERIFY(apply(d, ri).isEmpty());
        QCOMPARE(d.scans[0].values[1], 0.75f);
        QCOMPARE(d.scans[1].values[1], 1.25f);
    }
};

QTEST_GUILESS_MAIN(TestNoise)
#include "tst_noise.moc"
