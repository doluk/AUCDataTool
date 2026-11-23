// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Channel.h"
#include "auc/ScanTiming.h"
#include "auc/XlFormat.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace auc;
using namespace auc::timing;

namespace {

/// Cells visited in the order `cells`, one scan every `step` s; `skip` (cycle, cell) pairs
/// are left out.
std::vector<Slot> cycleRun(const std::vector<int>& cells, int cycles, double step, std::vector<std::pair<int, int>> skip = {})
{
    std::vector<Slot> cellSlots;
    for (int c : cells) cellSlots.push_back({c, 280.0, {}});
    double t = 100.0;
    for (int k = 0; k < cycles; ++k)
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (std::find(skip.begin(), skip.end(), std::pair{k, cells[i]}) != skip.end()) continue;
            cellSlots[i].scans.push_back({t, k + 1});
            t += step;
        }
    return cellSlots;
}

}  // namespace

class TestScanTiming : public QObject {
    Q_OBJECT
private slots:
    void medianValues()
    {
        QVERIFY(std::isnan(median({})));
        QCOMPARE(median({3.0}), 3.0);
        QCOMPARE(median({4.0, 1.0, 3.0}), 3.0);
        QCOMPARE(median({4.0, 1.0, 3.0, 2.0}), 2.5);
    }

    void regularCycle()
    {
        // Acquisition order 4 → 2 → 7, listed in another order.
        auto cellSlots = cycleRun({4, 2, 7}, 10, 20.0);
        std::swap(cellSlots[0], cellSlots[2]);
        const Analysis a = analyze(cellSlots);
        QCOMPARE(a.cellSlots.size(), std::size_t(3));
        QCOMPARE(a.cellSlots[0].cell, 4);
        QCOMPARE(a.cellSlots[1].cell, 2);
        QCOMPARE(a.cellSlots[2].cell, 7);
        QCOMPARE(a.events.size(), std::size_t(30));
        QCOMPARE(a.next.front(), 20.0);
        QVERIFY(std::isnan(a.next.back()));
        QCOMPARE(a.successor, (std::vector<int>{1, 2, 0}));
        QCOMPARE(a.transitions.size(), std::size_t(3));
        for (const Transition& t : a.transitions) {
            QVERIFY(t.regular);
            QCOMPARE(t.median, 20.0);
        }
        QCOMPARE(a.transitions[0].intervals.size(), std::size_t(10));  // 4→2
        QCOMPARE(a.transitions[2].intervals.size(), std::size_t(9));   // 7→4
        QVERIFY(a.skips.empty());
        for (double m : a.slotMedian) QCOMPARE(m, 60.0);
        QVERIFY(a.integerSeconds);
    }

    void skippedCell()
    {
        // Cell 5 missing from cycles 3 and 6: 4 → 7 instead of 4 → 5 → 7.
        const Analysis a = analyze(cycleRun({4, 5, 7, 1}, 10, 18.0, {{3, 5}, {6, 5}}));
        QCOMPARE(a.cellSlots[1].cell, 5);
        QCOMPARE(a.cellSlots[1].scans.size(), std::size_t(8));
        QCOMPARE(a.skips.size(), std::size_t(2));
        QCOMPARE(a.skips[0].from, 0);
        QCOMPARE(a.skips[0].to, 2);
        QCOMPARE(a.skips[0].skipped, std::vector<int>{1});
        QVERIFY(a.skips[0].seconds < a.skips[1].seconds);
        const Transition& last = a.transitions.back();
        QVERIFY(!last.regular);
        QCOMPARE(last.from, 0);
        QCOMPARE(last.to, 2);
        QCOMPARE(last.skipped, std::vector<int>{1});
        QCOMPARE(last.intervals.size(), std::size_t(2));
        QCOMPARE(last.median, 18.0);
        // Cycle 72 s (54 s without cell 5); cell 5 waits 72 + 54 s where it was skipped.
        QCOMPARE(a.slotMedian[0], 72.0);
        QCOMPARE(a.slotMedian[1], 72.0);
        const auto& s5 = a.cellSlots[1].scans;
        QCOMPARE(s5[3].seconds - s5[2].seconds, 126.0);
        QCOMPARE(s5[3].number - s5[2].number, 2);
    }

    void mergesDuplicateSlots()
    {
        // Sample and reference channel of the same intensity files give the same scans.
        auto cellSlots = cycleRun({1, 2}, 4, 30.0);
        cellSlots.push_back(cellSlots[0]);
        cellSlots.back().channel = 'B';
        const Analysis a = analyze(cellSlots);
        QCOMPARE(a.cellSlots.size(), std::size_t(2));
        QCOMPARE(a.events.size(), std::size_t(8));
        QVERIFY(a.integerSeconds);
    }

    void mergedChannelsListed()
    {
        // Channels B and A read in the same scans: one slot, both channels listed.
        std::vector<Slot> in;
        for (char ch : {'B', 'A'}) {
            Slot s{1, 280.0, {}, ch};
            for (int i = 0; i < 5; ++i) s.scans.push_back({100.0 * i, i + 1});
            in.push_back(std::move(s));
        }
        const Analysis a = analyze(std::move(in));
        QCOMPARE(a.cellSlots.size(), std::size_t(1));
        QCOMPARE(a.cellSlots[0].channels, std::string("AB"));
        QCOMPARE(a.cellSlots[0].channel, 'A');
    }

    void separateChannels()
    {
        // Multi-wavelength detector: channels A and B of a cell scanned one after the other,
        // all wavelengths at once.
        std::vector<Slot> s{{1, 220.0, {}, 'A', 600.0}, {1, 220.0, {}, 'B', 600.0}};
        for (int k = 0; k < 5; ++k) {
            s[0].scans.push_back({100.0 * k, k + 1});
            s[1].scans.push_back({100.0 * k + 40.5, k + 1});
        }
        const Analysis a = analyze(s);
        QCOMPARE(a.cellSlots.size(), std::size_t(2));
        QCOMPARE(a.cellSlots[1].channel, 'B');
        QCOMPARE(a.cellSlots[0].wavelengthTo, 600.0);
        QCOMPARE(a.events.size(), std::size_t(10));
        QCOMPARE(a.next[0], 40.5);
        QCOMPARE(a.slotMedian[1], 100.0);
        QVERIFY(!a.integerSeconds);
    }

    void empty()
    {
        QVERIFY(analyze({}).events.empty());
        QVERIFY(analyze({{1, 280.0, {}}}).cellSlots.empty());
    }

    void xlWavelengthScans()
    {
        // Two wavelength folders of cell 2; the second has one more scan.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString& sub, int scan, double wl, double seconds) {
            xl::ScanHeader h;
            h.type = DataType::RadialAbsorbance;
            h.cell = 2;
            h.rpm = 50000;
            h.seconds = seconds;
            h.wavelength = wl;
            std::vector<double> x{6.0, 6.5, 7.0};
            std::vector<float> v{0.1f, 0.2f, 0.3f};
            const QString path = dir.filePath(QStringLiteral("%1/%2.RA2").arg(sub).arg(scan, 5, 10, QLatin1Char('0')));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(xl::format(h, x, v));
        };
        for (int i = 1; i <= 3; ++i) write(QStringLiteral("2A280"), i, 280, 100.0 * i);
        for (int i = 1; i <= 4; ++i) write(QStringLiteral("2A230"), i, 230, 100.0 * i + 20);
        const OpenResult res = openData({dir.path()});
        QCOMPARE(res.channels.size(), std::size_t(1));
        const auto& c = *res.channels.front();
        QCOMPARE(c.scans.size(), std::size_t(3));
        const auto per = c.wavelengthScans();
        QCOMPARE(per.size(), std::size_t(2));
        QCOMPARE(per[0].size(), std::size_t(4));  // 230 nm
        QCOMPARE(per[1].size(), std::size_t(3));  // 280 nm
        QCOMPARE(per[0].back().seconds, 420.0);
        QCOMPARE(per[0].back().number, 4);
    }
};

QTEST_GUILESS_MAIN(TestScanTiming)
#include "tst_scantiming.moc"
