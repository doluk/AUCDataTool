// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
// aucinfo – summarise AUC data (.auc, .mwrs, .mw, XL .RA/.RI/.IP files or folders); optionally time reads.
#include "auc/Channel.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

#include <algorithm>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser p;
    p.setApplicationDescription("Summarise AUC data files or folders.");
    p.addHelpOption();
    p.addPositionalArgument("paths", "Files or folders", "paths...");
    const QCommandLineOption benchOpt("bench", "Time opening and wavelength-slice reads.");
    p.addOption(benchOpt);
    p.process(app);
    QTextStream out(stdout);
    if (p.positionalArguments().isEmpty()) p.showHelp(2);

    QElapsedTimer t;
    t.start();
    const auc::OpenResult res = auc::openData(p.positionalArguments());
    const double openMs = double(t.nsecsElapsed()) / 1e6;

    for (const auto& c : res.channels) {
        out << c->runId << "  cell " << c->cell << " channel " << c->channel << "  (" << c->formatName << ", "
            << (!c->valueLabel.isEmpty() ? c->valueLabel : c->absorbanceData ? QStringLiteral("absorbance") : QStringLiteral("intensity"))
            << ")\n";
        if (!c->description.isEmpty()) out << "  sample      " << c->description << "\n";
        out << "  folder      " << c->folder << "\n";
        if (!c->wavelengths.empty())
            out << "  wavelengths " << c->wavelengths.size() << ": " << c->wavelengths.front() << " … " << c->wavelengths.back()
                << " nm\n";
        if (!c->radius.empty())
            out << "  radius      " << c->radius.front() << " … " << c->radius.back() << " cm, " << c->radius.size() << " points\n";
        if (!c->scans.empty()) {
            const auto& a = c->scans.front();
            const auto& b = c->scans.back();
            out << "  scans       " << c->scans.size() << ", " << a.seconds << " … " << b.seconds << " s, " << a.rpm
                << " rpm (set " << a.setRpm << "), " << a.temperature << " °C\n";
        }
        if (!c->darkCurrent.empty())
            out << "  dark curr.  per wavelength, " << (c->darkSubtractedInFile ? "subtracted" : "not subtracted") << " in file\n";
    }
    for (const QString& w : res.warnings) out << "warning: " << w << "\n";

    if (p.isSet(benchOpt) && !res.channels.empty()) {
        std::size_t files = 0;
        for (const auto& c : res.channels) files += c->files.size();
        out << "\nopen: " << res.channels.size() << " channels, " << files << " files, headers read in " << openMs << " ms\n";
        const auto& c = res.channels.front();
        std::shared_ptr<const auc::Dataset> d;
        // Cold-ish first read, then a sweep over wavelengths (as when dragging the slider).
        t.restart();
        c->wavelengthSlice(c->wavelengths.size() / 2, d);
        out << "first slice (" << c->scans.size() << " scans × " << c->radius.size() << " points): " << t.nsecsElapsed() / 1e6
            << " ms\n";
        const std::size_t n = std::min<std::size_t>(c->wavelengths.size(), 100);
        t.restart();
        for (std::size_t k = 0; k < n; ++k) c->wavelengthSlice(k, d);
        out << "slice sweep: " << double(t.nsecsElapsed()) / 1e6 / double(n) << " ms per wavelength (" << n << " wavelengths)\n";
        t.restart();
        c->wavelengthMean(0, std::min<std::size_t>(c->wavelengths.size(), 21), d);
        out << "MWA mean over 21 wavelengths: " << t.nsecsElapsed() / 1e6 << " ms\n";
        const double mb = double(c->scans.size() * c->radius.size() * sizeof(float)) / 1e6;
        out << "memory per slice: " << mb << " MB (full channel would be " << mb * double(c->wavelengths.size()) << " MB)\n";
    }
    return res.channels.empty() ? 1 : 0;
}
