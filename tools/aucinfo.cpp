// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
// aucinfo – print header and scan summary of openAUC (.auc) files.
#include "auc/AucFile.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        out << "usage: aucinfo FILE.auc [...]\n";
        return 2;
    }
    int rc = 0;
    for (const QString& path : args) {
        auc::Dataset d;
        const auc::IoResult res = auc::AucFile::read(path, d);
        if (!res.ok()) {
            out << path << ": error: " << res.message << "\n";
            rc = 1;
            continue;
        }
        out << path << "\n"
            << "  type        " << QString::fromStdString(auc::toCode(d.type)) << "\n"
            << "  triple      " << QString::fromStdString(d.tripleName()) << "\n"
            << "  description " << QString::fromStdString(d.description) << "\n"
            << "  radius      " << d.radius.front() << " … " << d.radius.back() << " cm, " << d.pointCount()
            << " points\n"
            << "  scans       " << d.scanCount() << "\n";
        if (!d.scans.empty()) {
            const auto& a = d.scans.front();
            const auto& b = d.scans.back();
            out << "  time        " << a.seconds << " … " << b.seconds << " s\n"
                << "  ω²t         " << a.omega2t << " … " << b.omega2t << " rad²/s\n"
                << "  speed       " << a.rpm << " rpm, T " << a.temperature << " °C\n"
                << "  stddev      " << (a.stddev.empty() ? "no" : "yes") << "\n";
        }
        for (const auto& p : d.validate()) out << "  warning: " << QString::fromStdString(p) << "\n";
    }
    return rc;
}
