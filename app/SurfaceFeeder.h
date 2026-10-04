// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "AppController.h"

#include <QObject>
#include <QPointer>
#include <QtGraphs/QSurface3DSeries>
#include <QtQml/qqmlregistration.h>

/// Feeds the controller's surface grid (SurfaceGrid) into a Qt Graphs Surface3DSeries and
/// exposes axis ranges and titles for the Surface3D axes.
class SurfaceFeeder : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AppController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QSurface3DSeries* series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(double xMin READ xMin NOTIFY updated)
    Q_PROPERTY(double xMax READ xMax NOTIFY updated)
    Q_PROPERTY(double yMin READ yMin NOTIFY updated)
    Q_PROPERTY(double yMax READ yMax NOTIFY updated)
    Q_PROPERTY(double zMin READ zMin NOTIFY updated)
    Q_PROPERTY(double zMax READ zMax NOTIFY updated)
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
    double xMin() const { return m_xMin; }
    double xMax() const { return m_xMax; }
    double yMin() const { return m_grid ? m_grid->yMin : 0.0; }
    double yMax() const { return m_grid ? m_grid->yMax : 1.0; }
    double zMin() const { return m_zMin; }
    double zMax() const { return m_zMax; }
    QString xTitle() const { return m_grid ? m_grid->xTitle : QString(); }
    QString yTitle() const { return m_grid ? m_grid->yTitle : QString(); }
    QString zTitle() const { return m_grid ? m_grid->zTitle : QString(); }

signals:
    void controllerChanged();
    void seriesChanged();
    void updated();

private:
    void refresh();

    QPointer<AppController> m_controller;
    QPointer<QSurface3DSeries> m_series;
    SurfaceGridPtr m_grid;
    double m_xMin = 0, m_xMax = 1, m_zMin = 0, m_zMax = 1;
};
