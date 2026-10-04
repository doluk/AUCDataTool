#pragma once

#include "PlotSeries.h"

#include <QQuickItem>
#include <QRectF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// GPU-rendered multi-curve plot.
///
/// All curves are uploaded once into a single vertex buffer (line segments with per-vertex
/// colour) in data coordinates. Zooming and panning only change the matrix of a transform
/// node, so interaction costs O(1) regardless of how many scans are shown – the LabVIEW
/// viewer instead recoloured and redrew every plot individually.
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

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void updateTicks();

    PlotSeriesPtr m_series;
    QRectF m_view{0, 0, 1, 1};
    QVariantList m_xTicks, m_yTicks;
    std::vector<double> m_xTickPos, m_yTickPos;
    QColor m_gridColor{220, 220, 220};
    bool m_showGrid = true;
    bool m_dataDirty = false;
    bool m_gridDirty = true;
};
