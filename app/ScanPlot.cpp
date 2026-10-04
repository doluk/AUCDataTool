// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ScanPlot.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------------------
// PlotSeries
// ---------------------------------------------------------------------------------------

void PlotSeries::computeBounds()
{
    double x0 = std::numeric_limits<double>::max(), x1 = std::numeric_limits<double>::lowest();
    double y0 = x0, y1 = x1;
    for (float v : x) {
        if (!std::isfinite(v)) continue;
        x0 = std::min(x0, double(v));
        x1 = std::max(x1, double(v));
    }
    for (const auto& c : y) {
        for (float v : c) {
            if (!std::isfinite(v)) continue;
            y0 = std::min(y0, double(v));
            y1 = std::max(y1, double(v));
        }
    }
    if (x0 > x1) x0 = 0, x1 = 1;
    if (y0 > y1) y0 = 0, y1 = 1;
    if (x0 == x1) x0 -= 0.5, x1 += 0.5;
    if (y0 == y1) y0 -= 0.5, y1 += 0.5;
    xMin = x0, xMax = x1, yMin = y0, yMax = y1;
}

std::size_t PlotSeries::vertexCount() const
{
    return x.size() < 2 ? 0 : y.size() * 2 * (x.size() - 1);
}

// ---------------------------------------------------------------------------------------
// Tick generation
// ---------------------------------------------------------------------------------------

namespace {

/// "Nice" step (1, 2, 5 × 10ⁿ) giving roughly `target` intervals over `span`.
double niceStep(double span, int target)
{
    const double raw = span / std::max(target, 1);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    const double nice = n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10;
    return nice * mag;
}

QString tickLabel(double v, double step)
{
    if (std::abs(v) < step * 1e-6) v = 0;
    const int decimals = std::max(0, -int(std::floor(std::log10(step) + 1e-9)));
    if (std::abs(v) >= 1e5 || (std::abs(v) > 0 && std::abs(v) < 1e-3 && decimals > 4))
        return QString::number(v, 'g', 3);
    return QString::number(v, 'f', decimals);
}

void makeTicks(double lo, double hi, int target, std::vector<double>& pos, QVariantList& labels)
{
    pos.clear();
    labels.clear();
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return;
    const double step = niceStep(hi - lo, target);
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) {
        pos.push_back(v);
        QVariantMap m;
        m.insert(QStringLiteral("value"), v);
        m.insert(QStringLiteral("label"), tickLabel(v, step));
        labels.push_back(m);
    }
}

/// The scene graph addresses at most 65 535 vertices per geometry, so curves are split
/// over several nodes. Each holds an even number of vertices (whole line segments).
constexpr int kMaxVerticesPerNode = 65534;

/// Node tree: root → grid (pixel space) + transform (data → pixel) → curve chunks.
struct PlotRoot : QSGNode {
    QSGTransformNode* transform = nullptr;
    QSGGeometryNode* grid = nullptr;
};

QSGGeometryNode* makeCurveNode(int vertexCount)
{
    auto* node = new QSGGeometryNode;
    auto* geom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), vertexCount);
    geom->setDrawingMode(QSGGeometry::DrawLines);
    geom->setLineWidth(1.0f);
    // Static geometry stays in its GPU buffer; a changed transform matrix is only a
    // uniform update, so zoom/pan never re-uploads vertices.
    geom->setVertexDataPattern(QSGGeometry::StaticPattern);
    node->setGeometry(geom);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// ScanPlot
// ---------------------------------------------------------------------------------------

ScanPlot::ScanPlot(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setClip(true);
}

void ScanPlot::setSeries(PlotSeriesPtr series, bool keepView)
{
    const bool hadData = hasData();
    m_series = std::move(series);
    m_dataDirty = true;
    emit dataChanged();
    if (!keepView || !hadData)
        autoscale();
    else
        update();
}

void ScanPlot::setViewRect(const QRectF& r)
{
    if (!(r.width() > 0) || !(r.height() > 0) || r == m_view) return;
    m_view = r;
    m_gridDirty = true;
    updateTicks();
    emit viewRectChanged();
    update();
}

void ScanPlot::setGridColor(const QColor& c)
{
    if (c == m_gridColor) return;
    m_gridColor = c;
    m_gridDirty = true;
    emit gridColorChanged();
    update();
}

void ScanPlot::setShowGrid(bool on)
{
    if (on == m_showGrid) return;
    m_showGrid = on;
    m_gridDirty = true;
    emit showGridChanged();
    update();
}

void ScanPlot::autoscale()
{
    if (!m_series) return;
    const double padY = 0.04 * (m_series->yMax - m_series->yMin);
    setViewRect(QRectF(m_series->xMin, m_series->yMin - padY, m_series->xMax - m_series->xMin,
                       m_series->yMax - m_series->yMin + 2 * padY));
}

double ScanPlot::toDataX(double px) const { return m_view.left() + px / std::max(width(), 1.0) * m_view.width(); }
double ScanPlot::toDataY(double py) const
{
    return m_view.top() + (1.0 - py / std::max(height(), 1.0)) * m_view.height();
}
double ScanPlot::toPixelX(double x) const { return (x - m_view.left()) / m_view.width() * width(); }
double ScanPlot::toPixelY(double y) const { return (1.0 - (y - m_view.top()) / m_view.height()) * height(); }

void ScanPlot::zoomAt(double px, double py, double fx, double fy)
{
    const double cx = toDataX(px), cy = toDataY(py);
    const double w = m_view.width() / fx, h = m_view.height() / fy;
    const double tx = px / std::max(width(), 1.0), ty = 1.0 - py / std::max(height(), 1.0);
    setViewRect(QRectF(cx - tx * w, cy - ty * h, w, h));
}

void ScanPlot::panByPixels(double dx, double dy)
{
    const double ddx = dx / std::max(width(), 1.0) * m_view.width();
    const double ddy = dy / std::max(height(), 1.0) * m_view.height();
    setViewRect(m_view.translated(-ddx, ddy));
}

void ScanPlot::zoomToPixelRect(double x1, double y1, double x2, double y2)
{
    if (std::abs(x2 - x1) < 4 || std::abs(y2 - y1) < 4) return;
    const double a = toDataX(std::min(x1, x2)), b = toDataX(std::max(x1, x2));
    const double c = toDataY(std::max(y1, y2)), d = toDataY(std::min(y1, y2));
    setViewRect(QRectF(a, c, b - a, d - c));
}

void ScanPlot::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        m_gridDirty = true;
        updateTicks();
        update();
    }
}

void ScanPlot::updateTicks()
{
    const int tx = std::clamp(int(width() / 90.0), 2, 12);
    const int ty = std::clamp(int(height() / 60.0), 2, 10);
    makeTicks(m_view.left(), m_view.right(), tx, m_xTickPos, m_xTicks);
    makeTicks(m_view.top(), m_view.bottom(), ty, m_yTickPos, m_yTicks);
    emit ticksChanged();
}

QSGNode* ScanPlot::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* root = static_cast<PlotRoot*>(old);
    if (!root) {
        root = new PlotRoot;

        root->grid = new QSGGeometryNode;
        auto* gridGeom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        gridGeom->setDrawingMode(QSGGeometry::DrawLines);
        root->grid->setGeometry(gridGeom);
        root->grid->setFlag(QSGNode::OwnsGeometry);
        root->grid->setMaterial(new QSGFlatColorMaterial);
        root->grid->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(root->grid);

        root->transform = new QSGTransformNode;
        root->appendChildNode(root->transform);
        m_dataDirty = m_gridDirty = true;
    }

    // Curves: rebuilt only when the data changes.
    if (m_dataDirty) {
        m_dataDirty = false;
        while (QSGNode* child = root->transform->firstChild()) {
            root->transform->removeChildNode(child);
            delete child;
        }
        const std::size_t total = m_series ? m_series->vertexCount() : 0;
        std::size_t remaining = total;
        QSGGeometryNode* node = nullptr;
        QSGGeometry::ColoredPoint2D* v = nullptr;
        int room = 0;
        auto nextNode = [&] {
            const int n = int(std::min<std::size_t>(remaining, kMaxVerticesPerNode));
            node = makeCurveNode(n);
            root->transform->appendChildNode(node);
            v = node->geometry()->vertexDataAsColoredPoint2D();
            room = n;
        };
        if (total) {
            const auto& xs = m_series->x;
            for (std::size_t c = 0; c < m_series->y.size(); ++c) {
                const auto& ys = m_series->y[c];
                const QColor col = c < m_series->colors.size() ? m_series->colors[c] : QColor(Qt::black);
                const uchar r = uchar(col.red()), gr = uchar(col.green()), b = uchar(col.blue());
                for (std::size_t j = 1; j < xs.size(); ++j) {
                    if (room == 0) nextNode();
                    // NaN segments (e.g. masked points) collapse to a degenerate line.
                    float y0 = j - 1 < ys.size() ? ys[j - 1] : 0.f;
                    float y1 = j < ys.size() ? ys[j] : 0.f;
                    if (!std::isfinite(y0)) y0 = std::isfinite(y1) ? y1 : 0.f;
                    if (!std::isfinite(y1)) y1 = y0;
                    (v++)->set(xs[j - 1], y0, r, gr, b, 255);
                    (v++)->set(xs[j], y1, r, gr, b, 255);
                    room -= 2;
                    remaining -= 2;
                }
            }
        }
    }

    // Transform: data → pixels. y is flipped (pixel y grows downward).
    QMatrix4x4 m;
    const double sx = width() / m_view.width();
    const double sy = -height() / m_view.height();
    m.translate(float(-m_view.left() * sx), float(height() - m_view.top() * sy));
    m.scale(float(sx), float(sy));
    root->transform->setMatrix(m);

    // Grid: a handful of lines in pixel space, rebuilt when view or size changes.
    if (m_gridDirty) {
        m_gridDirty = false;
        // Fixed vertex count: a changing count alters the node structure and makes the
        // renderer rebuild all batches – including re-uploading every curve buffer.
        constexpr int kMaxGridLines = 64;
        QSGGeometry* g = root->grid->geometry();
        if (g->vertexCount() != 2 * kMaxGridLines) g->allocate(2 * kMaxGridLines);
        auto* v = g->vertexDataAsPoint2D();
        int used = 0;
        if (m_showGrid) {
            for (double x : m_xTickPos) {
                if (used == kMaxGridLines) break;
                const float px = float(std::round(toPixelX(x)) + 0.5);
                (v++)->set(px, 0);
                (v++)->set(px, float(height()));
                ++used;
            }
            for (double y : m_yTickPos) {
                if (used == kMaxGridLines) break;
                const float py = float(std::round(toPixelY(y)) + 0.5);
                (v++)->set(0, py);
                (v++)->set(float(width()), py);
                ++used;
            }
        }
        for (; used < kMaxGridLines; ++used) {  // unused lines collapse to a point off-screen
            (v++)->set(-10, -10);
            (v++)->set(-10, -10);
        }
        static_cast<QSGFlatColorMaterial*>(root->grid->material())->setColor(m_gridColor);
        root->grid->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }
    return root;
}
