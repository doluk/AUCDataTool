// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/AucFile.h"
#include "auc/Dataset.h"

#include <QString>

#include <vector>

/// Systematic noise vectors, loaded from files and subtracted from the data.
///
/// - Time-invariant (TI) noise: one value per radius point, the same for every scan
///   (optics, window scratches). Usually covers only the edited radius range
///   [minRadius, maxRadius] of the analysis that produced it.
/// - Radially invariant (RI) noise: one value per scan, the same for every radius point
///   (baseline jitter between scans).
///
/// Supported files:
/// - UltraScan III noise XML: <NoiseData><noise type="ti|ri" minradius=".." maxradius="..">
///   <d v="..."/>…</noise></NoiseData>
/// - Plain text/CSV: one value per line, or two columns. For TI the first column is the
///   radius (cm), for RI the scan number or time (ignored). Lines starting with '#' are
///   comments; ',', ';', tab and space separate columns.
namespace auc::noise {

enum class Type { TimeInvariant, RadiallyInvariant };

struct NoiseVector {
    Type type = Type::TimeInvariant;
    std::vector<double> values;
    double minRadius = 0.0;  ///< TI only; 0 = unknown (values must then cover all points)
    double maxRadius = 0.0;
    QString description;
    QString noiseGuid;
    QString modelGuid;
};

/// Reads a noise file. `expected` is the type for plain-text files; an XML file whose
/// declared type differs from `expected` is rejected (a TI file loaded as RI is a mistake).
IoResult readNoiseFile(const QString& path, Type expected, NoiseVector& out);

/// Writes UltraScan-compatible noise XML.
IoResult writeNoiseFile(const QString& path, const NoiseVector& noise);

/// Subtracts (remove=true) or adds the noise. Returns an empty string on success, else a
/// reason why the vector does not fit the dataset (nothing is changed then).
///
/// TI: if the vector is shorter than the scan, it is placed starting at the radius point
/// nearest to minRadius; maxRadius must then match the last covered point within half a
/// radial step. RI: the vector length must equal the dataset's scan count.
QString apply(Dataset& d, const NoiseVector& noise, bool remove = true);

}  // namespace auc::noise
