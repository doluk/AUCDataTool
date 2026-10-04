// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>

#include <memory>
#include <vector>

/// Immutable data handed from the controller to a plot: N curves on a shared x axis.
/// Shared between threads by std::shared_ptr<const PlotSeries>, never modified after creation.
struct PlotSeries {
    std::vector<float> x;                 ///< shared abscissa (e.g. radius)
    std::vector<std::vector<float>> y;    ///< one curve per entry, same length as x
    std::vector<QColor> colors;           ///< one per curve
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;  ///< data bounds (finite values only)

    void computeBounds();
    std::size_t vertexCount() const;
};

using PlotSeriesPtr = std::shared_ptr<const PlotSeries>;
