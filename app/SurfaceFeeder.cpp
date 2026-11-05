// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "SurfaceFeeder.h"

#include <QtGraphs/QSurfaceDataProxy>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct Range {
    double lo, hi;
    bool contains(double v) const { return v >= lo && v <= hi; }
};

/// Smallest value > 0 of `values`, +inf if none.
template <typename Values>
double positiveMin(const Values& values)
{
    double m = std::numeric_limits<double>::infinity();
    for (float v : values)
        if (v > 0) m = std::min(m, double(v));
    return m;
}

/// Limits of `axis` for data in [lo, hi]: the locked ones, else the data range. On a log
/// axis data reaching 0 or below starts at the smallest positive value `posMin`, but at most
/// kLogDecades below the maximum (noise around 0 would flatten everything else).
constexpr double kLogDecades = 3;

Range limits(const PlotAxis* axis, double lo, double hi, double posMin)
{
    const bool log = axis->log();
    if (log && !(lo > 0)) lo = std::max(posMin, hi * std::pow(10.0, -kLogDecades));
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) {
        if (std::isfinite(lo) && lo == hi) hi = log ? lo * 10 : lo + 1;
        else lo = log ? 1 : 0, hi = log ? 10 : 1;
    }
    lo = axis->limitOr(false, lo);
    hi = axis->limitOr(true, hi);
    if (!(hi > lo)) {
        if (axis->maxLocked() && !axis->minLocked()) lo = log ? hi / 10 : hi - 1;
        else hi = log ? lo * 10 : lo + 1;
    }
    return {lo, hi};
}

}  // namespace

SurfaceFeeder::SurfaceFeeder(QObject* parent)
    : QObject(parent)
    , m_xAxis(new PlotAxis(this))
    , m_yAxis(new PlotAxis(this))
    , m_zAxis(new PlotAxis(this))
{
    for (PlotAxis* a : {m_xAxis, m_yAxis, m_zAxis}) {
        connect(a, &PlotAxis::logChanged, this, &SurfaceFeeder::rebuild);
        connect(a, &PlotAxis::locksChanged, this, &SurfaceFeeder::rebuild);
    }
}

void SurfaceFeeder::setController(AppController* c)
{
    if (c == m_controller) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);
    m_controller = c;
    if (c) connect(c, &AppController::surfaceChanged, this, &SurfaceFeeder::refresh);
    emit controllerChanged();
    refresh();
}

void SurfaceFeeder::setSeries(QSurface3DSeries* s)
{
    if (s == m_series) return;
    m_series = s;
    emit seriesChanged();
    refresh();
}

void SurfaceFeeder::refresh()
{
    SurfaceGridPtr g = m_controller ? m_controller->surfaceGrid() : nullptr;
    if (g && (g->rows() < 2 || g->cols() < 2)) g = nullptr;
    m_grid = g;
    rebuild();
}

void SurfaceFeeder::rebuild()
{
    if (!m_series) return;
    const SurfaceGridPtr g = m_grid;
    if (!g) {
        m_series->dataProxy()->resetArray();
        emit updated();
        return;
    }
    const auto [x0, x1] = std::minmax_element(g->x.begin(), g->x.end());
    const auto [z0, z1] = std::minmax_element(g->z.begin(), g->z.end());
    const Range xr = limits(m_xAxis, *x0, *x1, m_xAxis->log() ? positiveMin(g->x) : 0.0);
    const Range zr = limits(m_zAxis, *z0, *z1, m_zAxis->log() ? positiveMin(g->z) : 0.0);
    const Range yr = limits(m_yAxis, g->yMin, g->yMax, m_yAxis->log() ? positiveMin(g->y) : 0.0);

    // Rows and columns inside the x/z limits; heights clamped to the y limits.
    std::vector<std::size_t> cols;
    for (std::size_t c = 0; c < g->cols(); ++c)
        if (xr.contains(g->x[c])) cols.push_back(c);
    QSurfaceDataArray rows;
    if (cols.size() >= 2) {
        rows.reserve(qsizetype(g->rows()));
        const std::size_t nc = g->cols();
        for (std::size_t r = 0; r < g->rows(); ++r) {
            if (!zr.contains(g->z[r])) continue;
            QSurfaceDataRow row;
            row.reserve(qsizetype(cols.size()));
            for (std::size_t c : cols) {
                float y = g->y[r * nc + c];
                if (std::isfinite(y)) y = float(std::clamp(double(y), yr.lo, yr.hi));
                row.append(QSurfaceDataItem(g->x[c], y, g->z[r]));
            }
            rows.append(std::move(row));
        }
        if (rows.size() < 2) rows.clear();
    }
    m_xAxis->setRange(xr.lo, xr.hi);
    m_yAxis->setRange(yr.lo, yr.hi);
    m_zAxis->setRange(zr.lo, zr.hi);
    if (rows.isEmpty()) m_series->dataProxy()->resetArray();
    else m_series->dataProxy()->resetArray(std::move(rows));
    emit updated();
}
