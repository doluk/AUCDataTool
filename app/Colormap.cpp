// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Colormap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

struct Rgb {
    double r, g, b;
};

// Viridis sampled at 9 points (matplotlib), linearly interpolated.
constexpr std::array<Rgb, 9> kViridis{{
    {0.267, 0.005, 0.329},
    {0.283, 0.141, 0.458},
    {0.254, 0.265, 0.530},
    {0.207, 0.372, 0.553},
    {0.164, 0.471, 0.558},
    {0.128, 0.567, 0.551},
    {0.135, 0.659, 0.518},
    {0.267, 0.749, 0.441},
    {0.993, 0.906, 0.144},
}};

Rgb lerp(const Rgb& a, const Rgb& b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb viridis(double t)
{
    // The last segment is steep in the original map; split it for a closer match.
    static constexpr Rgb kYellowGreen{0.478, 0.821, 0.318};
    const double x = t * 8.0;
    const int i = std::min(int(x), 7);
    const double f = x - i;
    if (i == 7) return f < 0.5 ? lerp(kViridis[7], kYellowGreen, f * 2.0) : lerp(kYellowGreen, kViridis[8], f * 2.0 - 1.0);
    return lerp(kViridis[size_t(i)], kViridis[size_t(i + 1)], f);
}

// Turbo, polynomial approximation (Mikhailov, Google 2019).
Rgb turbo(double t)
{
    const double r = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234 + t * (-152.94239396 + t * 59.28637943))));
    const double g = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333 + t * (4.27729857 + t * 2.82956604))));
    const double b = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771 + t * (-89.90310912 + t * 27.34824973))));
    return {r, g, b};
}

}  // namespace

QColor Colormap::color(Kind k, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    Rgb c{};
    switch (k) {
    case Viridis: c = viridis(t); break;
    case Turbo: c = turbo(t); break;
    case Rainbow: return QColor::fromHsvF(float(0.8 * (1.0 - t)), 1.0f, 0.9f);
    case Grey: c = {0.75 - 0.6 * t, 0.75 - 0.6 * t, 0.75 - 0.6 * t}; break;
    }
    return QColor::fromRgbF(float(std::clamp(c.r, 0.0, 1.0)), float(std::clamp(c.g, 0.0, 1.0)),
                            float(std::clamp(c.b, 0.0, 1.0)));
}
