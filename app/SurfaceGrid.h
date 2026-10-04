// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QString>

#include <memory>
#include <vector>

/// Regular grid for the 3D surface view: y[row · x.size() + col] at (x[col], z[row]).
/// Built on the worker thread, handed to the GUI thread read-only.
struct SurfaceGrid {
    std::vector<float> x, z, y;
    QString xTitle, yTitle, zTitle;
    QString title;
    float yMin = 0.f, yMax = 1.f;
    bool clipped = false;  ///< y limited to a robust (percentile) range

    std::size_t rows() const { return z.size(); }
    std::size_t cols() const { return x.size(); }
};

using SurfaceGridPtr = std::shared_ptr<const SurfaceGrid>;
