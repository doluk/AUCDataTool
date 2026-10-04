// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/AucFile.h"
#include "auc/Channel.h"
#include "auc/MwlFormat.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace auc;

namespace {

mwl::ScanHeader header(int cell, char ch, int scan, int points, int nwl)
{
    mwl::ScanHeader h;
    h.cell = cell;
    h.channel = ch;
    h.scan = scan;
    h.rpm = 39987;
    h.setRpm = 40000;
    h.temperature = 20.3;
    h.omega2t = 1.5e9 * scan;
    h.seconds = 120 * scan;
    h.points = points;
    h.rStart = 5.8;
    h.rStep = 0.0015;
    for (int k = 0; k < nwl; ++k) h.wavelengths.push_back(250 + 2 * k);
    return h;
}

/// rows[λ][point] = cell·1e6 + λ·1000 + point·10 + scan (unique, recognisable values)
std::vector<std::vector<std::int32_t>> rows(int cell, int scan, int points, int nwl)
{
    std::vector<std::vector<std::int32_t>> r(static_cast<size_t>(nwl), std::vector<std::int32_t>(static_cast<size_t>(points)));
    for (int k = 0; k < nwl; ++k)
        for (int j = 0; j < points; ++j) r[size_t(k)][size_t(j)] = cell * 1000000 + k * 1000 + j * 10 + scan;
    return r;
}

void writeBytes(const QString& path, const QByteArray& b)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(b);
}

}  // namespace

class TestMwl : public QObject {
    Q_OBJECT
private slots:
    void mwrsHeaderRoundTrip_data()
    {
        QTest::addColumn<double>("version");
        QTest::addColumn<bool>("intensity");
        QTest::addColumn<double>("scale");
        QTest::newRow("1.4 intensity") << 1.4 << true << 1.0;
        QTest::newRow("1.4 absorbance") << 1.4 << false << 0.0001;
        QTest::newRow("1.3") << 1.3 << true << 1.0;
        QTest::newRow("1.2") << 1.2 << true << 0.001;
        QTest::newRow("1.0 LabVIEW layout") << 1.0 << true << 0.001;
    }
    void mwrsHeaderRoundTrip()
    {
        QFETCH(double, version);
        QFETCH(bool, intensity);
        QFETCH(double, scale);
        const auto h = header(3, 'B', 7, 20, 6);
        const QByteArray bytes = mwl::encodeMwrs(h, rows(3, 7, 20, 6), version);
        mwl::MwrsRunInfo run;
        run.version = version;
        run.takeIntensity = intensity;
        mwl::ScanHeader back;
        const IoResult r = mwl::parseMwrsHeader(bytes, bytes.size(), run, back);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(back.cell, 3);
        QCOMPARE(back.channel, 'B');
        QCOMPARE(back.scan, 7);
        QCOMPARE(back.points, 20);
        QCOMPARE(back.wavelengths.size(), std::size_t(6));
        QCOMPARE(back.wavelengths[5], 260.0);
        QVERIFY(std::abs(back.rStep - 0.0015) < 1e-9);
        QVERIFY(std::abs(back.temperature - 20.3) < 1e-9);
        QCOMPARE(back.rpm, 39987.0);
        if (version > 1.05) QCOMPARE(back.setRpm, 40000.0);
        QCOMPARE(back.scale, scale);
        // first reading of λ index 2
        std::vector<float> v(20);
        mwl::decodeValues(bytes.constData() + back.dataOffset + 2 * 20 * 4, 20, 4, 2, back.scale, v.data());
        QVERIFY(std::abs(v[1] - float((3 * 1000000 + 2000 + 10 + 7) * scale)) <= float(std::abs(3e6 * scale) * 1e-6));
    }

    void mwrs10UltraScanLayout()
    {
        // v1.0 as read by UltraScan: nλ u16 at byte 22, λ u16 (nm).
        auto h = header(1, 'A', 1, 4, 3);
        QByteArray b = mwl::encodeMwrs(h, rows(1, 1, 4, 3), 1.0);  // LabVIEW layout …
        // … rewrite as UltraScan layout
        QByteArray us = b.left(22);
        us.append(char(0)).append(char(3));
        for (int wl : {250, 252, 254}) us.append(char(wl >> 8)).append(char(wl & 0xFF));
        us.append(b.mid(26 + 3 * 4));
        mwl::MwrsRunInfo run;
        run.version = 1.0;
        mwl::ScanHeader back;
        QVERIFY(mwl::parseMwrsHeader(us, us.size(), run, back).ok());
        QCOMPARE(back.wavelengths.size(), std::size_t(3));
        QCOMPARE(back.wavelengths[2], 254.0);
        QCOMPARE(back.dataOffset, qint64(24 + 6));
    }

    void rejectsIncompleteFile()
    {
        const auto h = header(1, 'A', 1, 50, 10);
        const QByteArray b = mwl::encodeMwrs(h, rows(1, 1, 50, 10), 1.4);
        mwl::ScanHeader back;
        mwl::MwrsRunInfo run;
        QCOMPARE(mwl::parseMwrsHeader(b.left(b.size() - 100), b.size() - 100, run, back).code, IoResult::NotAucFile);
    }

    void mw12WithDarkCurrent()
    {
        auto h = header(2, 'A', 4, 30, 5);
        h.description = "BSA 1 mg/ml";
        h.wavelengths = {230.5, 231.0, 231.5, 232.0, 232.5};
        h.darkSubtracted = true;
        const std::vector<std::int32_t> dark{11, 12, 13, 14, 15};
        const QByteArray b = mwl::encodeMw12(h, rows(2, 4, 30, 5), dark);
        mwl::ScanHeader back;
        const IoResult r = mwl::parseMwHeader(b, b.size(), back);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(back.variant, QString("mw 1.2"));
        QCOMPARE(back.description, QString("BSA 1 mg/ml"));
        QCOMPARE(back.wavelengths[1], 231.0);
        QCOMPARE(back.darkCurrent.size(), std::size_t(5));
        QCOMPARE(back.darkCurrent[4], 15.f);
        QVERIFY(back.darkSubtracted);
        QCOMPARE(back.setRpm, 40000.0);
        QVERIFY(std::abs(back.omega2t - 6e9) < 1e4 + 1);
        QCOMPARE(back.dateTime, QString("04.10.2026 12:00:00"));
    }

    void runXmlRoundTrip()
    {
        QTemporaryDir dir;
        mwl::MwrsRunInfo info;
        info.version = 1.3;
        info.takeIntensity = false;
        info.runId = "LD_2026-10-04";
        info.samples.insert("1A", "BSA");
        info.samples.insert("1B", "buffer");
        info.samples.insert("2A", "IgG");
        QVERIFY(mwl::writeMwrsXml(dir.filePath("run.mwrs.xml"), info).ok());
        mwl::MwrsRunInfo back;
        QVERIFY(mwl::readMwrsXml(dir.filePath("run.mwrs.xml"), back).ok());
        QCOMPARE(back.version, 1.3);
        QVERIFY(!back.takeIntensity);
        QCOMPARE(back.runId, QString("LD_2026-10-04"));
        QCOMPARE(back.samples.value("1B"), QString("buffer"));
        QCOMPARE(back.samples.value("2A"), QString("IgG"));
    }

    void openRunAndReadSlices()
    {
        QTemporaryDir dir;
        mwl::MwrsRunInfo info;
        info.version = 1.4;
        info.runId = "testrun";
        info.samples.insert("1A", "sample one");
        info.samples.insert("1B", "reference one");
        info.samples.insert("2A", "sample two");
        info.samples.insert("2B", "reference two");
        QVERIFY(mwl::writeMwrsXml(dir.filePath("testrun.mwrs.xml"), info).ok());
        const int points = 12, nwl = 8, nscan = 4;
        for (int cell : {1, 2})
            for (char ch : {'A', 'B'})
                for (int scan = 1; scan <= nscan; ++scan)
                    writeBytes(dir.filePath(QString("testrun.%1.%2.x.%3.mwrs").arg(cell).arg(ch).arg(scan, 5, 10, QChar('0'))),
                               mwl::encodeMwrs(header(cell, ch, scan, points, nwl), rows(cell, scan, points, nwl), 1.4));

        const OpenResult res = openData({dir.path()});
        QVERIFY2(res.warnings.isEmpty(), qPrintable(res.warnings.join("; ")));
        QCOMPARE(res.channels.size(), std::size_t(4));
        const auto& c = res.channels[2];  // sorted: 1A 1B 2A 2B
        QCOMPARE(c->key(), QString("2A"));
        QCOMPARE(c->description, QString("sample two"));
        QCOMPARE(c->format, SourceFormat::Mwrs);
        QVERIFY(!c->absorbanceData);
        QCOMPARE(c->scans.size(), std::size_t(nscan));
        QCOMPARE(c->wavelengths.size(), std::size_t(nwl));
        QCOMPARE(c->radius.size(), std::size_t(points));
        QCOMPARE(c->nearestWavelength(255.2), std::size_t(3));  // 250,252,254,256 → 256

        std::shared_ptr<const Dataset> d;
        QVERIFY(c->wavelengthSlice(3, d).ok());
        QCOMPARE(d->scanCount(), std::size_t(nscan));
        QCOMPARE(d->scans[2].values[5], float(2 * 1000000 + 3 * 1000 + 50 + 3));
        QCOMPARE(d->scans[0].wavelength, 256.0);
        QCOMPARE(d->type, DataType::RadialIntensity);

        // MWA mean over λ indices 2..4 → mean of k·1000 = 3000
        QVERIFY(c->wavelengthMean(2, 3, d).ok());
        QCOMPARE(d->scans[1].values[0], float(2 * 1000000 + 3000 + 0 + 2));
        QCOMPARE(d->scans[0].wavelength, 256.0);

        // Cached slice is the same object
        std::shared_ptr<const Dataset> a, b;
        QVERIFY(c->wavelengthSlice(1, a).ok());
        QVERIFY(c->wavelengthSlice(1, b).ok());
        QCOMPARE(a.get(), b.get());

        // Live: a new scan file appears
        writeBytes(dir.filePath("testrun.2.A.x.00005.mwrs"), mwl::encodeMwrs(header(2, 'A', 5, points, nwl), rows(2, 5, points, nwl), 1.4));
        QVERIFY(c->refresh());
        QCOMPARE(c->scans.size(), std::size_t(5));
    }

    void opensMwFilesWithDarkCurrent()
    {
        QTemporaryDir dir;
        for (int scan = 1; scan <= 3; ++scan) {
            auto h = header(1, 'A', scan, 16, 4);
            h.description = "sample";
            h.darkSubtracted = false;
            writeBytes(dir.filePath(QString("A%1.mw").arg(scan, 4, 10, QChar('0'))),
                       mwl::encodeMw12(h, rows(1, scan, 16, 4), {100, 200, 300, 400}));
        }
        const OpenResult res = openData({dir.path()});
        QVERIFY2(res.warnings.isEmpty(), qPrintable(res.warnings.join("; ")));
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = res.channels.front();
        QCOMPARE(c->format, SourceFormat::Mw);
        QCOMPARE(c->scans.size(), std::size_t(3));
        QCOMPARE(c->darkCurrent.size(), std::size_t(4));
        QCOMPARE(c->darkCurrent[2], 300.f);
        QVERIFY(!c->darkSubtractedInFile);
        QCOMPARE(c->description, QString("sample"));
        std::shared_ptr<const Dataset> d;
        QVERIFY(c->wavelengthSlice(2, d).ok());
        QCOMPARE(d->scans[2].values[1], float(1000000 + 2000 + 10 + 3));
    }

    void mwOmega2tScaleAndCellSuffix()
    {
        // Later acquisition versions store ω²t ÷1000 instead of ÷10000 and name files
        // "A001.MW3" (cell number in the extension). Stored raw value 1.8·10^6: ×10000 would
        // exceed ω²·t (2.1·10^9 at 39987 rpm, 120 s), so ×1000 is used.
        QTemporaryDir dir;
        auto h = header(3, 'A', 1, 16, 4);
        h.omega2t = 1.8e10;  // the encoder divides by 10000 → raw 1.8e6
        writeBytes(dir.filePath("A001.MW3"), mwl::encodeMw12(h, rows(3, 1, 16, 4), {0, 0, 0, 0}));
        h = header(3, 'A', 2, 16, 4);  // plausible as ÷10000: kept
        writeBytes(dir.filePath("A002.MW3"), mwl::encodeMw12(h, rows(3, 2, 16, 4), {0, 0, 0, 0}));
        const OpenResult res = openData({dir.path()});
        QVERIFY2(res.warnings.isEmpty(), qPrintable(res.warnings.join("; ")));
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = res.channels.front();
        QCOMPARE(c->format, SourceFormat::Mw);
        QCOMPARE(c->scans[0].omega2t, 1.8e9);
        QCOMPARE(c->scans[1].omega2t, 3.0e9);
    }

    void mwrsIntensityScaleFromMagnitude()
    {
        // "1.3" intensity runs whose readings are ×10000 (detector counts never reach 5·10^6).
        QTemporaryDir dir;
        mwl::MwrsRunInfo info;
        info.version = 1.3;
        info.takeIntensity = true;
        QVERIFY(mwl::writeMwrsXml(dir.filePath("r.mwrs.xml"), info).ok());
        auto big = rows(1, 1, 12, 4);
        for (auto& row : big)
            for (auto& v : row) v = 30000 * 10000 + v;  // 30000 counts ×10000
        writeBytes(dir.filePath("r.1.A.s.00001.mwrs"), mwl::encodeMwrs(header(1, 'A', 1, 12, 4), big, 1.3));
        const OpenResult res = openData({dir.path()});
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = res.channels.front();
        QVERIFY(c->formatName.contains("10000"));
        std::shared_ptr<const Dataset> d;
        QVERIFY(c->wavelengthSlice(0, d).ok());
        QVERIFY(std::abs(d->scans[0].values[0] - 30100.f) < 0.05f);  // (3e8 + 1000001) ÷ 10000
    }

    void groupsAucFilesByWavelength()
    {
        QTemporaryDir dir;
        for (double wl : {280.0, 230.0, 260.0}) {
            Dataset d;
            d.type = DataType::RadialIntensity;
            d.cell = 1;
            d.channel = 'A';
            for (int j = 0; j < 10; ++j) d.radius.push_back(6.0 + 0.01 * j);
            for (int i = 0; i < 3; ++i) {
                Scan s;
                s.wavelength = wl;
                s.seconds = 100 * (i + 1);
                for (int j = 0; j < 10; ++j) s.values.push_back(float(wl + j));
                d.scans.push_back(s);
            }
            QVERIFY(AucFile::write(dir.filePath(QString("run.RI.1.A.%1.auc").arg(int(wl))), d).ok());
        }
        const OpenResult res = openData({dir.path()});
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = res.channels.front();
        QCOMPARE(c->wavelengths.size(), std::size_t(3));
        QCOMPARE(c->wavelengths.front(), 230.0);
        QVERIFY(!c->absorbanceData);
        std::shared_ptr<const Dataset> d;
        QVERIFY(c->wavelengthSlice(2, d).ok());  // 280 nm
        QVERIFY(std::abs(d->scans[1].values[3] - 283.f) < 0.01f);
    }
};

QTEST_GUILESS_MAIN(TestMwl)
#include "tst_mwl.moc"
