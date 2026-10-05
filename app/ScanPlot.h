// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "CurveStyle.h"
#include "PlotSeries.h"

#include <QHash>

#include <QQuickItem>
#include <QRectF>
#include <QSGGeometry>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// GPU-rendered multi-curve plot.
///
/// All curves are uploaded once into a single vertex buffer (line segments with per-vertex
/// colour) in data coordinates. Zooming and panning only change the matrix of a transform
/// node, so interaction costs O(1) regardless of how many scans are shown – the LabVIEW
/// viewer instead recoloured and redrew every plot individually.
///
/// Curves with a non-default style (width > 1, dashes, markers, hidden) and the selected curve
/// are drawn as triangles in pixel space instead, rebuilt on view changes for the visible
/// x range only, so the fast path stays O(1) for the common case.
///
/// Axes labels/ticks are produced as `xTicks`/`yTicks` for QML to render; the grid is drawn
/// here so it stays aligned with the data.
class ScanPlot : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QRectF viewRect READ viewRect WRITE setViewRect NOTIFY viewRectChanged)
    Q_PROPERTY(QVariantList xTicks READ xTicks NOTIFY ticksChanged)
    Q_PROPERTY(QVariantList yTicks READ yTicks NOTIFY ticksChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(int curveCount READ curveCount NOTIFY dataChanged)
    Q_PROPERTY(qint64 vertexCount READ vertexCount NOTIFY dataChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    /// Curve styling. Curves are addressed by id (PlotSeries::ids, i.e. the scan index).
    Q_PROPERTY(int selectedCurve READ selectedCurve WRITE setSelectedCurve NOTIFY stylesChanged)
    /// Index of the selected curve in the current series, −1 if none.
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY stylesChanged)
    /// True if any curve of the current series has an individual style.
    Q_PROPERTY(bool hasCustomStyles READ hasCustomStyles NOTIFY stylesChanged)
    /// Incremented on every data or style change; bindings calling curveInfo() depend on it.
    Q_PROPERTY(int styleRevision READ styleRevision NOTIFY stylesChanged)
    /// Resolved style of the selected curve (empty map if none).
    Q_PROPERTY(QVariantMap selectedStyle READ selectedStyle NOTIFY stylesChanged)
    /// Style used by curves without an individual style (its colour is ignored: colormap).
    Q_PROPERTY(QVariantMap defaultStyle READ defaultStyleMap WRITE setDefaultStyleMap NOTIFY stylesChanged)

public:
    explicit ScanPlot(QQuickItem* parent = nullptr);

    void setSeries(PlotSeriesPtr series, bool keepView = false);
    PlotSeriesPtr series() const { return m_series; }

    /// Visible data range: x = xMin, width = xMax − xMin, y = yMin, height = yMax − yMin.
    QRectF viewRect() const { return m_view; }
    void setViewRect(const QRectF& r);

    QVariantList xTicks() const { return m_xTicks; }
    QVariantList yTicks() const { return m_yTicks; }
    bool hasData() const { return m_series && !m_series->y.empty(); }
    int curveCount() const { return m_series ? int(m_series->y.size()) : 0; }
    qint64 vertexCount() const { return m_series ? qint64(m_series->vertexCount()) : 0; }
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor& c);
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on);

    int selectedCurve() const { return m_selected; }
    void setSelectedCurve(int id);
    int selectedIndex() const { return curveIndex(m_selected); }
    bool hasCustomStyles() const;
    int styleRevision() const { return m_styleRevision; }
    /// { id, label, color, custom, visible } of the curve at `index`. Per row instead of one
    /// list for all curves: building and converting a list of every curve on each change
    /// cost O(curves) per list row, i.e. seconds for runs with > 1000 scans.
    Q_INVOKABLE QVariantMap curveInfo(int index) const;
    QVariantMap selectedStyle() const;
    QVariantMap defaultStyleMap() const { return m_defaultStyle.toMap(); }
    void setDefaultStyleMap(const QVariantMap& changes);
    /// Individual styles, saved/restored per channel by the controller.
    QHash<int, CurveStyle> curveStyles() const { return m_overrides; }
    void setCurveStyles(const QHash<int, CurveStyle>& styles);

    /// Id of the visible curve nearest to the pixel position, −1 if none within a few px.
    Q_INVOKABLE int curveAt(double px, double py) const;
    /// Applies the keys of `changes` (see CurveStyle::toMap) to the curve's style.
    Q_INVOKABLE void setCurveStyle(int id, const QVariantMap& changes);
    Q_INVOKABLE void resetCurveStyle(int id);
    Q_INVOKABLE void resetCurveStyles();

    Q_INVOKABLE void autoscale();
    /// Zoom by `factor` (>1 zooms in) around the pixel position (px, py).
    Q_INVOKABLE void zoomAt(double px, double py, double factorX, double factorY);
    Q_INVOKABLE void panByPixels(double dx, double dy);
    /// Zoom to a rectangle given in item pixels.
    Q_INVOKABLE void zoomToPixelRect(double x1, double y1, double x2, double y2);
    Q_INVOKABLE double toDataX(double px) const;
    Q_INVOKABLE double toDataY(double py) const;
    Q_INVOKABLE double toPixelX(double x) const;
    Q_INVOKABLE double toPixelY(double y) const;

signals:
    void viewRectChanged();
    void ticksChanged();
    void dataChanged();
    void gridColorChanged();
    void showGridChanged();
    void stylesChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void updateTicks();
    void stylesEdited();
    int curveId(std::size_t c) const;
    int curveIndex(int id) const;
    CurveStyle resolvedStyle(std::size_t c) const;
    /// Index range [first, last) of x values inside [x0, x1] (whole range if x is not sorted).
    std::pair<std::size_t, std::size_t> visibleRange(double x0, double x1) const;
    void buildStyledVertices();

    PlotSeriesPtr m_series;
    QRectF m_view{0, 0, 1, 1};
    QVariantList m_xTicks, m_yTicks;
    std::vector<double> m_xTickPos, m_yTickPos;
    QColor m_gridColor{220, 220, 220};
    bool m_showGrid = true;
    bool m_dataDirty = false;
    bool m_gridDirty = true;
    bool m_styledDirty = true;
    int m_xOrder = 0;  ///< 1 ascending, −1 descending, 0 unsorted

    CurveStyle m_defaultStyle;
    QHash<int, CurveStyle> m_overrides;
    int m_selected = -1;
    int m_styleRevision = 0;
    QHash<int, int> m_idIndex;  ///< curve id → index in the series
    /// Pixel-space triangles of styled curves, filled on the render thread.
    std::vector<QSGGeometry::ColoredPoint2D> m_styledVertices;
};
