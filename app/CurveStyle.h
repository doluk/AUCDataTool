// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QVariantMap>

#include <algorithm>

/// Appearance of one plotted curve. QML sees it as a map with the keys of toMap().
struct CurveStyle {
    enum Line { Solid, Dash, Dot, DashDot, NoLine };
    enum Marker { NoMarker, Circle, Square, Triangle, Diamond, Plus };

    QColor color;            ///< invalid: colour from the colormap
    float width = 1.0f;      ///< line width, px
    int line = Solid;
    int marker = NoMarker;
    float markerSize = 6.0f; ///< px
    bool visible = true;

    /// Drawable by the static 1-px line buffer (no per-view geometry needed).
    bool isPlain() const { return visible && line == Solid && width <= 1.0f && marker == NoMarker; }

    bool operator==(const CurveStyle&) const = default;

    QVariantMap toMap() const
    {
        return {{QStringLiteral("color"), color},
                {QStringLiteral("width"), width},
                {QStringLiteral("line"), line},
                {QStringLiteral("marker"), marker},
                {QStringLiteral("markerSize"), markerSize},
                {QStringLiteral("visible"), visible}};
    }

    /// Returns a copy with the keys present in `m` applied.
    CurveStyle merged(const QVariantMap& m) const
    {
        CurveStyle s = *this;
        if (auto it = m.find(QStringLiteral("color")); it != m.end()) s.color = it->value<QColor>();
        if (auto it = m.find(QStringLiteral("width")); it != m.end()) s.width = std::clamp(it->toFloat(), 0.5f, 20.0f);
        if (auto it = m.find(QStringLiteral("line")); it != m.end()) s.line = std::clamp(it->toInt(), 0, int(NoLine));
        if (auto it = m.find(QStringLiteral("marker")); it != m.end()) s.marker = std::clamp(it->toInt(), 0, int(Plus));
        if (auto it = m.find(QStringLiteral("markerSize")); it != m.end()) s.markerSize = std::clamp(it->toFloat(), 2.0f, 40.0f);
        if (auto it = m.find(QStringLiteral("visible")); it != m.end()) s.visible = it->toBool();
        return s;
    }
};
