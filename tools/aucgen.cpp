// aucgen – synthesise sedimentation-velocity data as openAUC (.auc) files.
//
// Concentration profile: Faxén-type approximation of the Lamm equation for one
// non-interacting species,
//   c(r,t) = c0 · exp(−2sω²t) · ½·erfc((r_b(t) − r) / (2√(Dt))),  r_b = r_m·exp(sω²t),
// plus optional time-invariant noise (fixed radial pattern), radially invariant noise
// (per-scan baseline jitter), Gaussian noise and a meniscus spike.
#include "auc/AucFile.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

#include <cmath>
#include <numbers>
#include <random>

namespace {

struct Species {
    double s;   // s
    double D;   // cm²/s
    double c0;  // OD
};

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription("Generate synthetic sedimentation-velocity data (.auc).");
    p.addHelpOption();
    p.addPositionalArgument("outdir", "Output directory");
    const QCommandLineOption scansOpt("scans", "Number of scans (default 200).", "n", "200");
    const QCommandLineOption pointsOpt("points", "Radius points (default 800).", "n", "800");
    const QCommandLineOption rpmOpt("rpm", "Rotor speed (default 40000).", "rpm", "40000");
    const QCommandLineOption intervalOpt("interval", "Seconds between scans (default 60).", "s", "60");
    const QCommandLineOption noiseOpt("noise", "Gaussian noise SD in OD (default 0.005).", "od", "0.005");
    const QCommandLineOption tiOpt("ti", "TI noise amplitude in OD (default 0.01).", "od", "0.01");
    const QCommandLineOption riOpt("ri", "RI noise SD in OD (default 0.005).", "od", "0.005");
    const QCommandLineOption cellsOpt("cells", "Number of cells (default 1).", "n", "1");
    const QCommandLineOption seedOpt("seed", "Random seed (default 1).", "n", "1");
    p.addOptions({scansOpt, pointsOpt, rpmOpt, intervalOpt, noiseOpt, tiOpt, riOpt, cellsOpt, seedOpt});
    p.process(app);

    QTextStream err(stderr);
    if (p.positionalArguments().size() != 1) p.showHelp(2);
    const QString outDir = p.positionalArguments().first();
    if (!QDir().mkpath(outDir)) {
        err << "cannot create " << outDir << "\n";
        return 1;
    }

    const int scans = p.value(scansOpt).toInt();
    const int points = p.value(pointsOpt).toInt();
    const double rpm = p.value(rpmOpt).toDouble();
    const double interval = p.value(intervalOpt).toDouble();
    const double noise = p.value(noiseOpt).toDouble();
    const double tiAmp = p.value(tiOpt).toDouble();
    const double riSd = p.value(riOpt).toDouble();
    const int cells = std::clamp(p.value(cellsOpt).toInt(), 1, 8);

    std::mt19937 rng(p.value(seedOpt).toUInt());
    std::normal_distribution<double> gauss(0.0, 1.0);

    const double rm = 5.95, rb = 7.15;  // meniscus, cell bottom (cm)
    const double omega = 2.0 * std::numbers::pi * rpm / 60.0;
    const double accelW2t = 3.2681e7 * std::expm1(7.55e-5 * rpm);  // same model as the viewer

    for (int cell = 1; cell <= cells; ++cell) {
        // Each cell gets a different two-species mixture.
        const std::vector<Species> species = {
            {(3.5 + cell) * 1e-13, 6.0e-7, 0.45},
            {(9.0 + 2 * cell) * 1e-13, 3.5e-7, 0.30},
        };

        auc::Dataset d;
        d.type = auc::DataType::RadialAbsorbance;
        d.cell = cell;
        d.channel = 'A';
        d.description = "aucgen synthetic data, cell " + std::to_string(cell);
        for (auto& g : d.guid) g = static_cast<std::uint8_t>(rng());
        d.radius.resize(size_t(points));
        const double dr = (rb - 5.8) / (points - 1);
        for (int j = 0; j < points; ++j) d.radius[size_t(j)] = 5.8 + j * dr;

        std::vector<double> ti(static_cast<size_t>(points), 0.0);
        for (int j = 0; j < points; ++j) {
            const double r = d.radius[size_t(j)];
            ti[size_t(j)] = tiAmp * (std::sin(37.0 * r) + 0.5 * std::sin(113.0 * r + 1.3));
        }

        for (int i = 0; i < scans; ++i) {
            auc::Scan s;
            s.seconds = 300.0 + i * interval;
            s.rpm = rpm;
            s.temperature = 20.0 + 0.05 * gauss(rng);
            s.omega2t = accelW2t + omega * omega * s.seconds;
            s.wavelength = 280.0;
            s.deltaR = dr;
            const double ri = riSd * gauss(rng);
            const double t = s.seconds;
            s.values.resize(size_t(points));
            for (int j = 0; j < points; ++j) {
                const double r = d.radius[size_t(j)];
                double a = 0.0;
                if (r > rm) {
                    for (const auto& sp : species) {
                        const double ex = sp.s * s.omega2t;
                        const double rbound = rm * std::exp(ex);
                        a += sp.c0 * std::exp(-2.0 * ex) * 0.5 * std::erfc((rbound - r) / (2.0 * std::sqrt(sp.D * t)));
                    }
                }
                // Meniscus: a sharp negative/positive optical artefact.
                a += -0.6 * std::exp(-std::pow((r - rm) / 0.004, 2));
                a += ti[size_t(j)] + ri + noise * gauss(rng);
                s.values[size_t(j)] = float(a);
            }
            d.scans.push_back(std::move(s));
        }

        const QString file = QDir(outDir).filePath(QStringLiteral("synthetic.RA.%1.A.280.auc").arg(cell));
        const auc::IoResult res = auc::AucFile::write(file, d);
        if (!res.ok()) {
            err << file << ": " << res.message << "\n";
            return 1;
        }
        QTextStream(stdout) << "wrote " << file << "\n";
    }
    return 0;
}
