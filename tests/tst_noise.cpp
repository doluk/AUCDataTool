#include "auc/Noise.h"

#include <QTest>

#include <cmath>
#include <random>

using namespace auc;

class TestNoise : public QObject {
    Q_OBJECT
private slots:
    void recoversExactDecomposition()
    {
        const int ns = 20, np = 50;
        std::vector<double> b(np), beta(ns);
        for (int j = 0; j < np; ++j) b[size_t(j)] = 0.01 * std::sin(0.3 * j) + 0.002;
        double mean = 0.0;
        for (int i = 0; i < ns; ++i) mean += (beta[size_t(i)] = 0.005 * std::cos(1.7 * i));
        mean /= ns;
        for (double& v : beta) v -= mean;  // convention: Σβ = 0

        Dataset e;
        for (int j = 0; j < np; ++j) e.radius.push_back(6.0 + 0.01 * j);
        for (int i = 0; i < ns; ++i) {
            Scan s;
            for (int j = 0; j < np; ++j) s.values.push_back(float(b[size_t(j)] + beta[size_t(i)]));
            e.scans.push_back(s);
        }

        const auto r = noise::fit(e);
        QCOMPARE(r.ti.size(), std::size_t(np));
        QCOMPARE(r.ri.size(), std::size_t(ns));
        for (int j = 0; j < np; ++j) QVERIFY(std::abs(r.ti[size_t(j)] - b[size_t(j)]) < 1e-6);
        for (int i = 0; i < ns; ++i) QVERIFY(std::abs(r.ri[size_t(i)] - beta[size_t(i)]) < 1e-6);
        QVERIFY(r.rmsAfter < 1e-6);
        QVERIFY(r.rmsBefore > 1e-3);

        noise::subtract(e, r);
        for (const auto& s : e.scans)
            for (float v : s.values) QVERIFY(std::abs(v) < 1e-6);
    }

    void fitAgainstModelReducesResiduals()
    {
        std::mt19937 rng(7);
        std::normal_distribution<double> g(0.0, 0.001);
        Dataset model, data;
        for (int j = 0; j < 100; ++j) model.radius.push_back(6.0 + 0.01 * j);
        data.radius = model.radius;
        for (int i = 0; i < 30; ++i) {
            Scan m, d;
            for (int j = 0; j < 100; ++j) {
                const double c = 0.5 * std::erfc((6.3 + 0.01 * i - model.radius[size_t(j)]) / 0.05);
                m.values.push_back(float(c));
                d.values.push_back(float(c + 0.02 * std::sin(0.5 * j) + 0.01 * (i % 3) + g(rng)));
            }
            model.scans.push_back(m);
            data.scans.push_back(d);
        }
        const auto r = noise::fitToModel(data, model);
        QVERIFY(r.rmsAfter < 0.002);           // only random noise remains
        QVERIFY(r.rmsBefore > 5 * r.rmsAfter);
    }

    void singleComponent()
    {
        Dataset e;
        e.radius = {1, 2};
        Scan s1, s2;
        s1.values = {1.f, 3.f};
        s2.values = {3.f, 5.f};
        e.scans = {s1, s2};
        const auto ti = noise::fit(e, noise::TimeInvariant);
        QVERIFY(ti.ri.empty());
        QCOMPARE(ti.ti[0], 2.0);
        const auto ri = noise::fit(e, noise::RadiallyInvariant);
        QVERIFY(ri.ti.empty());
        QCOMPARE(ri.ri[1], 4.0);
    }
};

QTEST_GUILESS_MAIN(TestNoise)
#include "tst_noise.moc"
