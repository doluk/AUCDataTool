// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/AucFile.h"
#include "auc/Dataset.h"

#include <QString>
#include <QStringList>

#include <vector>

/// Writers for processed data in the formats other programs read.
namespace auc::exporter {

/// Wavelength as used in file and folder names: "280", or "279.5" for fractional values.
QString wavelengthTag(double nm);

/// Beckman XL ASCII, one file per scan, in the XL/UltraScan export layout
/// `<dir>/<cell><channel><λ>/<channel><scan:5>.<RA|RI|IP|FI><cell>` (e.g. "2A280/A00012.RA2").
/// `scanNumbers` (optional) gives the number used in each file name; NaN points are skipped.
IoResult writeBeckman(const Dataset& d, const QString& dir, const std::vector<int>& scanNumbers = {},
                      QStringList* written = nullptr);

/// UltraScan III / openAUC: `<dir>/<runId>.<TYPE>.<cell>.<channel>.<λ>.auc`. NaN points are
/// written as 0 and flagged as interpolated.
IoResult writeUs3(const Dataset& d, const QString& dir, const QString& runId, QString* written = nullptr);

/// Origin ASCII import: tab-separated columns with three header rows (Long Name, Units,
/// Comments), first column x, then one column per scan. NaN is written as "--" (missing).
struct OriginColumns {
    QString xName, xUnit;
    QString yName, yUnit;
    std::vector<QString> comments;  ///< per scan (e.g. "t = 120 s"); empty = scan time
};
IoResult writeOrigin(const Dataset& d, const QString& path, const OriginColumns& cols);

}  // namespace auc::exporter
