// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

/// Colour maps for scan series. Viridis is perceptually uniform and colour-blind safe;
/// Rainbow mimics the scan colouring of the LabVIEW AUC-Viewer.
class Colormap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("enum holder")
public:
    enum Kind { Viridis, Turbo, Rainbow, Grey };
    Q_ENUM(Kind)

    /// t in [0, 1].
    static QColor color(Kind k, double t);
};
