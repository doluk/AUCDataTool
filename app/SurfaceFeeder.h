// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "AppController.h"
#include "PlotAxis.h"

#include <QObject>
#include <QPointer>
#include <QtGraphs/QSurface3DSeries>
#include <QtQml/qqmlregistration.h>

/// Feeds the controller's surface grid (SurfaceGrid) into a Qt Graphs Surface3DSeries and
/// exposes axis ranges and titles for the Surface3D axes.
///
/// Each axis (x: radius or wavelength, y: value, z: time or wavelength) is linear or
/// logarithmic; unlocked limits follow the data. The grid is cut to the x/z limits and the
/// values are clamped to the y limits, so the surface never leaves the plot box.
class SurfaceFeeder : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QSurface3DSeries* series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(PlotAxis* xAxis READ xAxis CONSTANT)
    Q_PROPERTY(PlotAxis* yAxis READ yAxis CONSTANT)
    Q_PROPERTY(PlotAxis* zAxis READ zAxis CONSTANT)
    Q_PROPERTY(QString xTitle READ xTitle NOTIFY updated)
    Q_PROPERTY(QString yTitle READ yTitle NOTIFY updated)
    Q_PROPERTY(QString zTitle READ zTitle NOTIFY updated)

public:
    explicit SurfaceFeeder(QObject* parent = nullptr);

    AppController* controller() const { return m_controller; }
    void setController(AppController* c);
    QSurface3DSeries* series() const { return m_series; }
    void setSeries(QSurface3DSeries* s);

    bool hasData() const { return m_grid != nullptr; }
    PlotAxis* xAxis() const { return m_xAxis; }
    PlotAxis* yAxis() const { return m_yAxis; }
    PlotAxis* zAxis() const { return m_zAxis; }
    QString xTitle() const { return m_grid ? m_grid->xTitle : QString(); }
    QString yTitle() const { return m_grid ? m_grid->yTitle : QString(); }
    QString zTitle() const { return m_grid ? m_grid->zTitle : QString(); }

signals:
    void controllerChanged();
    void seriesChanged();
    void updated();

private:
    void refresh();
    /// Builds the series from m_grid within the current axis limits.
    void rebuild();

    QPointer<AppController> m_controller;
    QPointer<QSurface3DSeries> m_series;
    SurfaceGridPtr m_grid;
    PlotAxis* m_xAxis;
    PlotAxis* m_yAxis;
    PlotAxis* m_zAxis;
};
