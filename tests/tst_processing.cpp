#include "auc/Processing.h"

#include <QTest>

#include <cmath>
#include <numbers>

using namespace auc;
using namespace auc::proc;

namespace {
bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

Dataset ramp(int scans, int points)
{
    Dataset d;
    for (int j = 0; j < points; ++j) d.radius.push_back(6.0 + 0.01 * j);
    for (int i = 0; i < scans; ++i) {
        Scan s;
        for (int j = 0; j < points; ++j) s.values.push_back(float(i + 0.1 * j));
        d.scans.push_back(s);
    }
    return d;
}
}  // namespace

class TestProcessing : public QObject {
    Q_OBJECT
private slots:
    void threshold()
    {
        const std::vector<double> x{1.0, 2.0, 4.0, 8.0};
        QVERIFY(near(thresholdIndex(x, 0.5), 0.0));  // below range
        QVERIFY(near(thresholdIndex(x, 1.0), 0.0));
        QVERIFY(near(thresholdIndex(x, 3.0), 1.5));
        QVERIFY(near(thresholdIndex(x, 4.0), 2.0));  // value ≤ x[i+1] picks the left pair
        QVERIFY(near(thresholdIndex(x, 9.0), 3.0));  // above range
        // LabVIEW "To Long Integer" rounds half to even.
        QCOMPARE(nearestIndex(x, 3.0), std::size_t(2));  // 1.5 → 2
        QCOMPARE(nearestIndex(std::vector<double>{0, 1, 2, 3}, 2.5), std::size_t(2));  // 2.5 → 2
    }

    void absorbanceFormula()
    {
        const std::vector<float> sample{1000.f, 100.f, 50.f, 5000.f};
        const std::vector<float> ref{10000.f, 10000.f, 10000.f, 50.f};
        std::vector<float> a(4);
        absorbance(sample, ref, a);
        QVERIFY(near(a[0], 1.0, 1e-6));  // −log10(0.1)
        QVERIFY(near(a[1], 3.0));        // I = 100 is not > 100 → invalid
        QVERIFY(near(a[2], 3.0));
        QVERIFY(near(a[3], 3.0));        // reference too low
    }

    void meanOfSelectedScans()
    {
        const Dataset d = ramp(4, 3);
        const auto all = meanScan(d);
        QVERIFY(near(all[0], 1.5, 1e-6));
        const std::vector<std::size_t> sel{1, 3};
        QVERIFY(near(meanScan(d, sel)[2], 2.0 + 0.2, 1e-6));
    }

    void darkCurrent()
    {
        std::vector<float> v{10.f, 20.f};
        const std::vector<float> dark{1.f, 2.f};
        applyDarkCurrent(v, dark, true);
        QVERIFY(near(v[1], 18.0));
        applyDarkCurrent(v, dark, false);
        QVERIFY(near(v[1], 20.0));
    }

    void offset()
    {
        Dataset d = ramp(2, 11);
        subtractOffsetAt(d, 6.05);  // index 5
        QVERIFY(near(d.scans[0].values[5], 0.0, 1e-6));
        QVERIFY(near(d.scans[1].values[0], -0.5, 1e-6));

        Dataset e = ramp(1, 11);
        subtractBaselineRegion(e, 6.0, 6.02);  // mean of points 0..2 = 0.1
        QVERIFY(near(e.scans[0].values[1], 0.0, 1e-6));
    }

    void medianMatchesNiSemantics()
    {
        // left rank 2, right rank 0; outside elements count as zero.
        const std::vector<float> x{5.f, 1.f, 9.f, 3.f};
        const auto y = medianFilter(x, 2, 0);
        const std::vector<float> expect{0.f, 1.f, 5.f, 3.f};
        for (std::size_t i = 0; i < x.size(); ++i) QCOMPARE(y[i], expect[i]);
        // Even window: mean of the middle pair.
        const auto z = medianFilter(std::vector<float>{4.f, 8.f}, 1, 0);
        QCOMPARE(z[1], 6.f);
        // A single spike is removed.
        const auto w = medianFilter(std::vector<float>{1, 1, 1, 50, 1, 1}, 2, 0);
        QCOMPARE(w[3], 1.f);
    }

    void selection()
    {
        const auto idx = selectScans(10, 2, 100, 3);
        const std::vector<std::size_t> expect{2, 5, 8};
        QVERIFY(idx == expect);
        QVERIFY(selectScans(0, 0, 5).empty());
    }

    void acceleration()
    {
        const std::vector<double> v{40000.0, 40000.0};
        const auto e = estimateAcceleration(v);
        QVERIFY(near(e.meanSpeed, 40000.0));
        QVERIFY(near(e.omega2t, 32681000.0 * std::expm1(3.02), 1e-3));
        QVERIFY(near(e.time, 49.49 + 48.4 + 42.29712, 1e-9));
    }

    void omega2tIntegral()
    {
        const std::vector<double> t{0.0, 100.0};
        const std::vector<double> rpm{60000.0, 60000.0};
        const double w = 2.0 * std::numbers::pi * 1000.0;
        QVERIFY(near(integrateOmega2t(t, rpm), w * w * 100.0, 1e-3));
    }

    void wavelengthBins()
    {
        const std::vector<float> wl{250.0f, 250.4f, 250.6f, 251.2f, 252.0f};
        const auto b = binWavelengths(wl);
        const std::vector<std::size_t> start{0, 2, 4}, count{2, 2, 1};
        QVERIFY(b.start == start);
        QVERIFY(b.count == count);
        QVERIFY(near(b.wavelength[0], 250.2, 1e-4));

        // Two points, three wavelengths → bins {0,1},{2}
        const std::vector<float> rows{1, 2, 3, 4, 10, 20};
        const auto bins = binWavelengths(std::vector<float>{300.0f, 300.2f, 301.0f});
        const auto avg = averageOverBins(rows, 2, bins);
        QCOMPARE(avg.size(), std::size_t(4));
        QCOMPARE(avg[0], 2.f);
        QCOMPARE(avg[1], 3.f);
        QCOMPARE(avg[3], 20.f);
    }

    void radialIntegral()
    {
        std::vector<double> r;
        std::vector<float> one, lin;
        for (int j = 0; j <= 100; ++j) {
            r.push_back(6.0 + 0.01 * j);
            one.push_back(1.f);
            lin.push_back(float(r.back()));
        }
        QVERIFY(near(integrateRadial(r, one, 6.0, 7.0), 1.0, 1e-9));
        QVERIFY(near(integrateRadial(r, one, 6.255, 6.505), 0.25, 1e-9));  // limits between points
        QVERIFY(near(integrateRadial(r, lin, 6.0, 7.0), (49.0 - 36.0) / 2.0, 1e-5));  // trapezoid exact for linear
        QVERIFY(near(integrateRadial(r, one, 6.0, 7.0, true), (49.0 - 36.0) / 2.0, 1e-9));
        QVERIFY(near(integrateRadial(r, one, 7.0, 6.0), -1.0, 1e-9));
        QVERIFY(near(integrateRadial(r, one, 8.0, 9.0), 0.0));
    }

    void reverse()
    {
        Dataset d = ramp(1, 3);
        reverseRadius(d);
        QVERIFY(near(d.radius.front(), 6.02));
        QVERIFY(near(d.scans[0].values.front(), 0.2, 1e-6));
    }
};

QTEST_GUILESS_MAIN(TestProcessing)
#include "tst_processing.moc"
