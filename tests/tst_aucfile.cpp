// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Crc32.h"
#include "auc/AucFile.h"

#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <cmath>
#include <cstring>

using namespace auc;

namespace {

Dataset makeDataset(bool withStdDev)
{
    Dataset d;
    d.type = DataType::RadialIntensity;
    d.cell = 3;
    d.channel = 'B';
    d.description = "round trip test";
    for (std::size_t k = 0; k < d.guid.size(); ++k) d.guid[k] = std::uint8_t(k * 7);
    for (int j = 0; j < 300; ++j) d.radius.push_back(5.9 + 0.003 * j);
    for (int i = 0; i < 5; ++i) {
        Scan s;
        s.temperature = 20.1 + i * 0.01;
        s.rpm = 42000;
        s.seconds = 120 + 60 * i;
        s.omega2t = 1.5e8 * (i + 1);
        s.wavelength = 259.6;
        s.deltaR = 0.003;
        for (int j = 0; j < 300; ++j) {
            s.values.push_back(float(1000.0 + 500.0 * std::sin(0.05 * j + i)));
            if (withStdDev) s.stddev.push_back(float(5.0 + 0.01 * j));
        }
        s.interpolated.assign((300 + 7) / 8, 0);
        s.interpolated[0] = 0x80;  // point 0 interpolated
        s.interpolated[1] = 0x01;  // point 15 interpolated
        d.scans.push_back(s);
    }
    return d;
}

/// Re-computes and stores the trailing CRC after a test patched the bytes.
void fixCrc(QByteArray& b)
{
    const quint32 crc = detail::crc32(0xFFFFFFFFu, b.constData(), size_t(b.size() - 4));
    qToLittleEndian<quint32>(crc, b.data() + b.size() - 4);
}

}  // namespace

class TestAucFile : public QObject {
    Q_OBJECT
private slots:
    void crcMatchesZlib()
    {
        // Standard check value of CRC-32/ISO-HDLC.
        QCOMPARE(detail::crc32(0, "123456789", 9), 0xCBF43926u);
        // Chaining must equal one pass.
        const quint32 a = detail::crc32(0xFFFFFFFFu, "12345", 5);
        QCOMPARE(detail::crc32(a, "6789", 4), detail::crc32(0xFFFFFFFFu, "123456789", 9));
    }

    void roundTrip_data()
    {
        QTest::addColumn<bool>("withStdDev");
        QTest::newRow("values") << false;
        QTest::newRow("values+stddev") << true;
    }
    void roundTrip()
    {
        QFETCH(bool, withStdDev);
        const Dataset src = makeDataset(withStdDev);
        QTemporaryDir dir;
        const QString path = dir.filePath("t.auc");
        QVERIFY(AucFile::write(path, src).ok());

        Dataset back;
        const IoResult res = AucFile::read(path, back);
        QVERIFY2(res.ok(), qPrintable(res.message));
        QCOMPARE(back.type, src.type);
        QCOMPARE(back.cell, 3);
        QCOMPARE(back.channel, 'B');
        QCOMPARE(back.description, src.description);
        QVERIFY(back.guid == src.guid);
        QCOMPARE(back.pointCount(), src.pointCount());
        QCOMPARE(back.scanCount(), src.scanCount());
        QVERIFY(std::abs(back.radius.back() - src.radius.back()) < 1e-4);

        // 16-bit quantisation over a 1000-count range: step ≈ 0.015.
        const double tol = 1000.0 / 65535.0;
        for (std::size_t i = 0; i < src.scans.size(); ++i) {
            const Scan& a = src.scans[i];
            const Scan& b = back.scans[i];
            QCOMPARE(b.seconds, a.seconds);
            QVERIFY(std::abs(b.wavelength - 259.6) < 1e-9);
            QVERIFY(std::abs(b.omega2t - a.omega2t) / a.omega2t < 1e-6);
            for (std::size_t j = 0; j < a.values.size(); ++j) QVERIFY(std::abs(b.values[j] - a.values[j]) <= tol);
            QCOMPARE(b.stddev.empty(), !withStdDev);
            QVERIFY(b.isInterpolated(0));
            QVERIFY(b.isInterpolated(15));
            QVERIFY(!b.isInterpolated(1));
        }
    }

    void detectsCorruption()
    {
        QByteArray bytes = AucFile::toBytes(makeDataset(false));
        bytes[400] = char(bytes[400] ^ 0x5A);
        Dataset d;
        QCOMPARE(AucFile::readFromBytes(bytes, d).code, IoResult::BadCrc);
    }

    void rejectsTruncated()
    {
        const QByteArray bytes = AucFile::toBytes(makeDataset(false));
        Dataset d;
        QCOMPARE(AucFile::readFromBytes(bytes.left(bytes.size() / 2), d).code, IoResult::NotAucFile);
        QCOMPARE(AucFile::readFromBytes(QByteArray("NOPE"), d).code, IoResult::NotAucFile);
    }

    void version4Wavelength()
    {
        // Version 4 stored (λ − 180)·100. Patch a v5 file into v4 encoding.
        QByteArray b = AucFile::toBytes(makeDataset(false));
        b[4] = '0';
        b[5] = '4';
        const qsizetype header = 4 + 2 + 2 + 1 + 1 + 16 + 240 + 28 + 2;
        const qsizetype wlOffset = header + 4 + 4 + 4 + 4 + 4;  // DATA, T, rpm, s, ω²t
        qToLittleEndian<quint16>(quint16(std::lround((259.6 - 180.0) * 100.0)), b.data() + wlOffset);
        fixCrc(b);
        Dataset d;
        const IoResult res = AucFile::readFromBytes(b, d);
        QVERIFY2(res.ok(), qPrintable(res.message));
        QVERIFY(std::abs(d.scans[0].wavelength - 259.6) < 1e-9);
    }

    void headerOnly()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("h.auc");
        QVERIFY(AucFile::write(path, makeDataset(false)).ok());
        AucFile::Header h;
        QVERIFY(AucFile::readHeader(path, h).ok());
        QCOMPARE(h.scanCount, 5);
        QCOMPARE(h.cell, 3);
        QCOMPARE(h.version, 5);
    }
};

QTEST_GUILESS_MAIN(TestAucFile)
#include "tst_aucfile.moc"
