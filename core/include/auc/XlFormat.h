// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "auc/AucFile.h"
#include "auc/Dataset.h"

#include <QByteArray>
#include <QString>

#include <span>
#include <vector>

/// Beckman XL-A/XL-I (and Optima export) ASCII scan files, one file per scan:
///
///     <description>                                   (IP: "cm/pixel: 0.0007046, <description>")
///     R 2 19.9 48000 0000393 7.9561E+09 280 1         type cell T rpm seconds ω²t λ replicates
///        5.8001  1.23502E+00   0.00000E+00           x value [third column]
///
/// Type letters: R radial absorbance, I radial intensity, P interference, F fluorescence,
/// W wavelength scan (absorbance or intensity, from the extension; the λ field then holds
/// the radius in cm and the first column the wavelength). The extension is the type code
/// and the cell number (".RA1", ".ri2", ".IP3").
///
/// The third column is the standard deviation for absorbance scans. Intensity scans of
/// double-sector cells store sample intensity in the second and reference intensity in
/// the third column (A = log₁₀(I_ref / I_sample)). Radial points are not equidistant and
/// differ from scan to scan.
namespace auc::xl {

struct ScanHeader {
    DataType type = DataType::RadialAbsorbance;
    char letter = 'R';
    int cell = 1;
    double temperature = 0;
    double rpm = 0;
    double seconds = 0;
    double omega2t = 0;
    double wavelength = 0;  ///< nm; radius in cm for wavelength scans
    int replicates = 1;
    QString description;
    int columns = 2;        ///< numeric columns of the first data line (2 or 3)
};

struct ScanFile {
    ScanHeader header;
    std::vector<double> x;    ///< radius (cm) or wavelength (nm)
    std::vector<float> value;
    std::vector<float> third; ///< empty if the file has two columns
};

/// True for "ra1", "RI2", "ip3", "wa1", "wi1", "fi1" … (case-insensitive suffix).
bool isXlSuffix(const QString& suffix);
/// File-name patterns for directory scans.
QStringList nameFilters();

/// Parses the two header lines and the first data line. `suffix` resolves W scans
/// (WA/WI) and is used when the type letter is unknown.
IoResult parseHeader(const QByteArray& head, const QString& suffix, ScanHeader& out);
IoResult readHeader(const QString& path, ScanHeader& out);
IoResult read(const QString& path, ScanFile& out);
IoResult parse(const QByteArray& bytes, const QString& suffix, ScanFile& out);

/// Formats one scan. `third` may be empty (written as zeros for 3-column types).
QByteArray format(const ScanHeader& h, std::span<const double> x, std::span<const float> value,
                  std::span<const float> third = {});
/// Extension for a data type and cell, e.g. "RA2".
QString extension(DataType t, int cell);

/// Linear interpolation of (x, y) onto `grid` (x ascending); NaN outside [x.front(), x.back()].
void resample(std::span<const double> x, std::span<const float> y, std::span<const double> grid, float* out);

}  // namespace auc::xl
