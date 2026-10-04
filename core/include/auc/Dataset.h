// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace auc {

/// Kind of measured quantity, as encoded in the two-character file type.
enum class DataType {
    RadialAbsorbance,       ///< "RA" – absorbance vs. radius
    RadialIntensity,        ///< "RI" – raw intensity vs. radius
    Interference,           ///< "IP" – Rayleigh interference
    Fluorescence,           ///< "FI" – fluorescence intensity
    WavelengthAbsorbance,   ///< "WA" – absorbance vs. wavelength
    WavelengthIntensity,    ///< "WI" – intensity vs. wavelength
};

/// Two-character code ("RA", "RI", ...) for a data type.
std::string toCode(DataType t);
/// Parses a two-character code; returns false for unknown codes.
bool fromCode(std::string_view code, DataType& out);

/// One recorded scan: readings on the dataset's common radius grid plus run conditions.
struct Scan {
    double temperature = 0.0;   ///< °C
    double rpm = 0.0;           ///< rotor speed, 1/min
    double seconds = 0.0;       ///< elapsed run time, s
    double omega2t = 0.0;       ///< ∫ω² dt, rad²/s
    double wavelength = 0.0;    ///< nm
    double deltaR = 0.0;        ///< radial step stored with the scan, cm

    std::vector<float> values;        ///< readings, one per radius point
    std::vector<float> stddev;        ///< empty if the file stores no standard deviations
    std::vector<std::uint8_t> interpolated;  ///< bitmap, bit j set = point j was interpolated

    bool isInterpolated(std::size_t j) const
    {
        const std::size_t byte = j / 8;
        return byte < interpolated.size() && (interpolated[byte] & (0x80u >> (j % 8)));
    }
};

/// One cell/channel/wavelength triple: a series of scans on a common radius grid.
struct Dataset {
    DataType type = DataType::RadialAbsorbance;
    int cell = 0;
    char channel = 'A';
    std::array<std::uint8_t, 16> guid{};
    std::string description;

    std::vector<double> radius;   ///< cm, ascending, shared by all scans
    std::vector<Scan> scans;

    std::size_t pointCount() const { return radius.size(); }
    std::size_t scanCount() const { return scans.size(); }

    /// Human-readable triple id, e.g. "2A 280 nm".
    std::string tripleName() const;

    /// Returns a list of problems (empty = consistent).
    std::vector<std::string> validate() const;
};

}  // namespace auc
