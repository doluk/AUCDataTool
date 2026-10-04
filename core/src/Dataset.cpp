// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "auc/Dataset.h"

#include <cmath>
#include <cstdio>

namespace auc {

namespace {
struct CodeEntry {
    DataType type;
    const char* code;
};
constexpr CodeEntry kCodes[] = {
    {DataType::RadialAbsorbance, "RA"},     {DataType::RadialIntensity, "RI"},
    {DataType::Interference, "IP"},         {DataType::Fluorescence, "FI"},
    {DataType::WavelengthAbsorbance, "WA"}, {DataType::WavelengthIntensity, "WI"},
};
}  // namespace

std::string toCode(DataType t)
{
    for (const auto& e : kCodes)
        if (e.type == t) return e.code;
    return "??";
}

bool fromCode(std::string_view code, DataType& out)
{
    for (const auto& e : kCodes) {
        if (code == e.code) {
            out = e.type;
            return true;
        }
    }
    return false;
}

std::string Dataset::tripleName() const
{
    char buf[64];
    const double wl = scans.empty() ? 0.0 : scans.front().wavelength;
    std::snprintf(buf, sizeof buf, "%d%c %g nm", cell, channel, std::round(wl * 10.0) / 10.0);
    return buf;
}

std::vector<std::string> Dataset::validate() const
{
    std::vector<std::string> problems;
    for (std::size_t j = 1; j < radius.size(); ++j) {
        if (!(radius[j] > radius[j - 1])) {
            problems.push_back("radius axis is not strictly ascending");
            break;
        }
    }
    for (std::size_t i = 0; i < scans.size(); ++i) {
        const auto& s = scans[i];
        if (s.values.size() != radius.size())
            problems.push_back("scan " + std::to_string(i) + ": value count does not match radius count");
        if (!s.stddev.empty() && s.stddev.size() != s.values.size())
            problems.push_back("scan " + std::to_string(i) + ": stddev count does not match value count");
    }
    return problems;
}

}  // namespace auc
