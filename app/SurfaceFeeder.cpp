// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "SurfaceFeeder.h"

#include <QtGraphs/QSurfaceDataProxy>

#include <algorithm>

SurfaceFeeder::SurfaceFeeder(QObject* parent)
    : QObject(parent)
{
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
    if (!m_series) return;
    SurfaceGridPtr g = m_controller ? m_controller->surfaceGrid() : nullptr;
    if (g && (g->rows() < 2 || g->cols() < 2)) g = nullptr;
    m_grid = g;
    if (!g) {
        m_series->dataProxy()->resetArray();
        emit updated();
        return;
    }
    QSurfaceDataArray rows;
    rows.reserve(qsizetype(g->rows()));
    const std::size_t nc = g->cols();
    for (std::size_t r = 0; r < g->rows(); ++r) {
        QSurfaceDataRow row;
        row.reserve(qsizetype(nc));
        for (std::size_t c = 0; c < nc; ++c) row.append(QSurfaceDataItem(g->x[c], g->y[r * nc + c], g->z[r]));
        rows.append(std::move(row));
    }
    const auto [x0, x1] = std::minmax_element(g->x.begin(), g->x.end());
    const auto [z0, z1] = std::minmax_element(g->z.begin(), g->z.end());
    m_xMin = *x0;
    m_xMax = *x1 > *x0 ? *x1 : *x0 + 1.0;
    m_zMin = *z0;
    m_zMax = *z1 > *z0 ? *z1 : *z0 + 1.0;
    m_series->dataProxy()->resetArray(std::move(rows));
    emit updated();
}
