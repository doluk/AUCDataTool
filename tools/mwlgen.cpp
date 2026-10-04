// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
// mwlgen – synthesise a multi-wavelength (.mwrs) run as written by the Cölfen MWL detector.
//
// Channel B holds the reference intensity I0(λ, r) (lamp spectrum × optics), channel A the
// sample intensity I = I0' · 10^(−A(λ, r, t)) with A = Σ c_s(r, t) · ε_s(λ) for two
// sedimenting species (Faxén approximation) with different spectra, plus counting noise.
#include "auc/MwlFormat.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

#include <cmath>
#include <numbers>
#include <random>

namespace {

struct Species {
    double s, D, c0;   // s, cm²/s, OD at band maximum
    double peak, width;  // absorption band (nm)
    double uv;         // strength of the far-UV rise
};

double epsilon(const Species& sp, double wl)
{
    const double band = std::exp(-0.5 * std::pow((wl - sp.peak) / sp.width, 2));
    const double farUv = sp.uv * std::exp(-(wl - 200.0) / 12.0);
    return band + farUv;
}

double lamp(double wl)
{
    // Xenon-flash-like: smooth continuum with a few lines; detector response falls in the UV.
    const double continuum = 0.35 + 0.65 * (1.0 - std::exp(-(wl - 190.0) / 60.0));
    const double lines = 0.25 * std::exp(-0.5 * std::pow((wl - 467.0) / 3.0, 2))
                       + 0.18 * std::exp(-0.5 * std::pow((wl - 529.0) / 4.0, 2));
    return std::max(0.05, continuum + lines);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription("Generate a synthetic multi-wavelength .mwrs run.");
    p.addHelpOption();
    p.addPositionalArgument("outdir", "Output directory (created)");
    const QCommandLineOption cellsO("cells", "Cells (default 2).", "n", "2");
    const QCommandLineOption scansO("scans", "Scans per channel (default 40).", "n", "40");
    const QCommandLineOption wlO("wavelengths", "Wavelengths, 1 nm apart from 220 nm (default 300).", "n", "300");
    const QCommandLineOption ptsO("points", "Radius points (default 800).", "n", "800");
    const QCommandLineOption verO("version", "File version 1.2, 1.3 or 1.4 (default 1.4).", "v", "1.4");
    const QCommandLineOption rpmO("rpm", "Rotor speed (default 40000).", "rpm", "40000");
    p.addOptions({cellsO, scansO, wlO, ptsO, verO, rpmO});
    p.process(app);
    if (p.positionalArguments().size() != 1) p.showHelp(2);

    QTextStream out(stdout), err(stderr);
    const QString dirPath = p.positionalArguments().first();
    if (!QDir().mkpath(dirPath)) {
        err << "cannot create " << dirPath << "\n";
        return 1;
    }
    QDir dir(dirPath);
    const int cells = std::clamp(p.value(cellsO).toInt(), 1, 8);
    const int nscan = p.value(scansO).toInt();
    const int nwl = p.value(wlO).toInt();
    const int np = p.value(ptsO).toInt();
    const double version = p.value(verO).toDouble();
    const double rpm = p.value(rpmO).toDouble();
    const double storeFactor = version < 1.25 ? 1000.0 : 1.0;  // v1.0–1.2 store counts ×1000

    const QString runId = QStringLiteral("synthetic_mwl");
    auc::mwl::MwrsRunInfo info;
    info.version = version;
    info.takeIntensity = true;
    info.runId = runId;
    for (int c = 1; c <= cells; ++c) {
        info.samples.insert(QStringLiteral("%1A").arg(c), QStringLiteral("protein + DNA, cell %1").arg(c));
        info.samples.insert(QStringLiteral("%1B").arg(c), QStringLiteral("buffer reference"));
    }
    if (auto r = auc::mwl::writeMwrsXml(dir.filePath(runId + ".mwrs.xml"), info); !r.ok()) {
        err << r.message << "\n";
        return 1;
    }

    std::mt19937 rng(42);
    std::normal_distribution<double> g(0.0, 1.0);
    const double rm = 5.95, rStart = 5.80, rEnd = 7.15;
    const double rStep = std::round((rEnd - rStart) / (np - 1) * 10000.0) / 10000.0;
    const double omega = 2.0 * std::numbers::pi * rpm / 60.0;
    std::vector<double> wl(static_cast<size_t>(nwl));
    for (int k = 0; k < nwl; ++k) wl[size_t(k)] = 220.0 + k;

    QElapsedTimer timer;
    timer.start();
    qint64 bytes = 0;
    for (int cell = 1; cell <= cells; ++cell) {
        const std::vector<Species> species = {
            {(4.0 + 0.5 * cell) * 1e-13, 6e-7, 0.5, 280.0, 9.0, 0.9},   // protein-like
            {(12.0 + cell) * 1e-13, 2.5e-7, 0.4, 260.0, 14.0, 0.6},     // nucleic-acid-like
        };
        // Optics: a weak radial vignetting and per-channel window transmission.
        std::vector<double> optics(static_cast<size_t>(np));
        for (int j = 0; j < np; ++j) optics[size_t(j)] = 0.92 + 0.06 * std::cos(3.0 * (rStart + j * rStep));
        std::vector<double> lampWl(static_cast<size_t>(nwl));
        for (int k = 0; k < nwl; ++k) lampWl[size_t(k)] = 30000.0 * lamp(wl[size_t(k)]);

        for (int scan = 1; scan <= nscan; ++scan) {
            const double t = 300.0 + 120.0 * (scan - 1);
            const double w2t = 3.2681e7 * std::expm1(7.55e-5 * rpm) + omega * omega * t;
            // Concentration profiles of this scan.
            std::vector<std::vector<double>> conc(species.size(), std::vector<double>(size_t(np), 0.0));
            for (std::size_t sIdx = 0; sIdx < species.size(); ++sIdx) {
                const auto& sp = species[sIdx];
                const double ex = sp.s * w2t;
                const double rb = rm * std::exp(ex);
                for (int j = 0; j < np; ++j) {
                    const double r = rStart + j * rStep;
                    if (r > rm)
                        conc[sIdx][size_t(j)] = sp.c0 * std::exp(-2.0 * ex) * 0.5 * std::erfc((rb - r) / (2.0 * std::sqrt(sp.D * t)));
                }
            }
            const double lampFlicker = 1.0 + 0.004 * g(rng);  // per-scan lamp intensity variation

            for (char ch : {'A', 'B'}) {
                auc::mwl::ScanHeader h;
                h.cell = cell;
                h.channel = ch;
                h.scan = scan;
                h.rpm = rpm + std::round(8.0 * g(rng));
                h.setRpm = rpm;
                h.temperature = 20.0 + 0.1 * g(rng);
                h.omega2t = w2t;
                h.seconds = t;
                h.points = np;
                h.rStart = rStart;
                h.rStep = rStep;
                h.wavelengths = wl;
                const double window = ch == 'A' ? 0.97 : 1.0;
                std::vector<std::vector<std::int32_t>> rows(static_cast<size_t>(nwl), std::vector<std::int32_t>(size_t(np)));
                for (int k = 0; k < nwl; ++k) {
                    double eps[2];
                    for (int sIdx = 0; sIdx < 2; ++sIdx) eps[sIdx] = epsilon(species[size_t(sIdx)], wl[size_t(k)]);
                    for (int j = 0; j < np; ++j) {
                        const double r = rStart + j * rStep;
                        double i0 = lampWl[size_t(k)] * optics[size_t(j)] * window * lampFlicker;
                        double absorbance = 0.0;
                        if (ch == 'A') {
                            absorbance = conc[0][size_t(j)] * eps[0] + conc[1][size_t(j)] * eps[1];
                            absorbance += 1.5 * std::exp(-std::pow((r - rm) / 0.004, 2));  // meniscus
                        }
                        double counts = i0 * std::pow(10.0, -absorbance);
                        counts += std::sqrt(std::max(counts, 1.0)) * g(rng) + 400.0;  // shot noise + dark level
                        rows[size_t(k)][size_t(j)] = std::int32_t(std::lround(std::max(counts, 0.0) * storeFactor));
                    }
                }
                const QByteArray b = auc::mwl::encodeMwrs(h, rows, version);
                const QString name = QStringLiteral("%1.%2.%3.S.%4.mwrs").arg(runId).arg(cell).arg(QLatin1Char(ch)).arg(scan, 5, 10, QLatin1Char('0'));
                QFile f(dir.filePath(name));
                if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size()) {
                    err << name << ": " << f.errorString() << "\n";
                    return 1;
                }
                bytes += b.size();
            }
        }
        out << "cell " << cell << " done\n";
        out.flush();
    }
    out << "wrote " << cells * 2 * nscan << " files, " << bytes / 1e6 << " MB in " << timer.elapsed() / 1000.0 << " s\n";
    return 0;
}
