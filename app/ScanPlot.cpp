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
#include <numbers>

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

/// Triangle chunks: a multiple of 3 (whole triangles) below the 16-bit index limit.
constexpr int kMaxTrianglesVertices = 65532;

/// Node tree: root → grid (pixel space) + transform (data → pixel) → curve chunks
///                  + styled (pixel space) → triangle chunks.
struct PlotRoot : QSGNode {
    QSGTransformNode* transform = nullptr;
    QSGGeometryNode* grid = nullptr;
    QSGNode* styled = nullptr;
    std::size_t styledVertices = 0;
};

using Vertex = QSGGeometry::ColoredPoint2D;

struct Rgba {
    uchar r, g, b, a;
    /// The vertex colour material expects premultiplied alpha.
    static Rgba from(const QColor& c, double alpha = 1.0)
    {
        const double a = c.alphaF() * alpha;
        return {uchar(c.red() * a), uchar(c.green() * a), uchar(c.blue() * a), uchar(255 * a)};
    }
};

/// Appends pixel-space triangles for lines and markers.
struct TriangleWriter {
    std::vector<Vertex>& out;
    Rgba c;

    void tri(QPointF a, QPointF b, QPointF d)
    {
        out.push_back({}); out.back().set(float(a.x()), float(a.y()), c.r, c.g, c.b, c.a);
        out.push_back({}); out.back().set(float(b.x()), float(b.y()), c.r, c.g, c.b, c.a);
        out.push_back({}); out.back().set(float(d.x()), float(d.y()), c.r, c.g, c.b, c.a);
    }
    void quad(QPointF a, QPointF b, QPointF d, QPointF e)  // a-b-d-e in order around
    {
        tri(a, b, d);
        tri(a, d, e);
    }
    /// Segment p→q of width w; `cap` extends both ends by w/2 so consecutive segments join.
    void segment(QPointF p, QPointF q, double w, bool cap)
    {
        const QPointF d = q - p;
        const double len = std::hypot(d.x(), d.y());
        if (len <= 0) return;
        const QPointF u = d / len;
        const QPointF n(-u.y() * w / 2, u.x() * w / 2);
        if (cap) {
            p -= u * (w / 2);
            q += u * (w / 2);
        }
        quad(p + n, q + n, q - n, p - n);
    }
    void marker(int kind, QPointF p, double size)
    {
        const double r = size / 2;
        switch (kind) {
        case CurveStyle::Circle: {
            constexpr int kSides = 12;
            QPointF prev = p + QPointF(r, 0);
            for (int i = 1; i <= kSides; ++i) {
                const double a = 2 * std::numbers::pi * i / kSides;
                const QPointF next = p + QPointF(r * std::cos(a), r * std::sin(a));
                tri(p, prev, next);
                prev = next;
            }
            break;
        }
        case CurveStyle::Square:
            quad(p + QPointF(-r, -r), p + QPointF(r, -r), p + QPointF(r, r), p + QPointF(-r, r));
            break;
        case CurveStyle::Triangle:
            tri(p + QPointF(0, -r), p + QPointF(r * 0.866, r * 0.5), p + QPointF(-r * 0.866, r * 0.5));
            break;
        case CurveStyle::Diamond:
            quad(p + QPointF(0, -r), p + QPointF(r, 0), p + QPointF(0, r), p + QPointF(-r, 0));
            break;
        case CurveStyle::Plus: {
            const double t = std::max(1.0, size / 5) / 2;
            quad(p + QPointF(-r, -t), p + QPointF(r, -t), p + QPointF(r, t), p + QPointF(-r, t));
            quad(p + QPointF(-t, -r), p + QPointF(t, -r), p + QPointF(t, r), p + QPointF(-t, r));
            break;
        }
        default:
            break;
        }
    }
};

/// Dash pattern in units of the line width (on, off, on, off, …); empty = solid.
std::vector<double> dashPattern(int line)
{
    switch (line) {
    case CurveStyle::Dash: return {4, 2.5};
    case CurveStyle::Dot: return {1, 2};
    case CurveStyle::DashDot: return {4, 2, 1, 2};
    default: return {};
    }
}

/// Walks a polyline and emits its "on" pieces according to a dash pattern.
struct Dasher {
    TriangleWriter& w;
    double width;
    std::vector<double> pattern;  // already scaled to px
    std::size_t piece = 0;
    double left = 0;

    Dasher(TriangleWriter& writer, double wd, int line)
        : w(writer), width(wd)
    {
        for (double v : dashPattern(line)) pattern.push_back(v * std::max(wd, 1.0));
        left = pattern.empty() ? 0 : pattern[0];
    }
    void segment(QPointF p, QPointF q)
    {
        if (pattern.empty()) {
            w.segment(p, q, width, true);
            return;
        }
        const QPointF d = q - p;
        const double len = std::hypot(d.x(), d.y());
        if (len <= 0) return;
        const QPointF u = d / len;
        double t = 0;
        while (t < len) {
            const double step = std::min(left, len - t);
            if (piece % 2 == 0) w.segment(p + u * t, p + u * (t + step), width, false);
            t += step;
            left -= step;
            if (left <= 1e-9) {
                piece = (piece + 1) % pattern.size();
                left = pattern[piece];
            }
        }
    }
};

double distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double l2 = QPointF::dotProduct(ab, ab);
    const double t = l2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    const QPointF d = p - (a + ab * t);
    return std::hypot(d.x(), d.y());
}

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
    m_dataDirty = m_styledDirty = true;
    ++m_styleRevision;
    m_idIndex.clear();
    if (m_series) {
        m_idIndex.reserve(qsizetype(m_series->ids.size()));
        for (std::size_t c = 0; c < m_series->ids.size(); ++c) m_idIndex.insert(m_series->ids[c], int(c));
    }
    m_xOrder = 0;
    if (m_series && m_series->x.size() > 1) {
        const auto& x = m_series->x;
        if (std::is_sorted(x.begin(), x.end())) m_xOrder = 1;
        else if (std::is_sorted(x.rbegin(), x.rend())) m_xOrder = -1;
    }
    if (m_selected >= 0 && curveIndex(m_selected) < 0) m_selected = -1;
    emit dataChanged();
    emit stylesChanged();
    if (!keepView || !hadData)
        autoscale();
    else
        update();
}

void ScanPlot::setViewRect(const QRectF& r)
{
    if (!(r.width() > 0) || !(r.height() > 0) || r == m_view) return;
    m_view = r;
    m_gridDirty = m_styledDirty = true;
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

int ScanPlot::curveId(std::size_t c) const
{
    return m_series && c < m_series->ids.size() ? m_series->ids[c] : int(c);
}

int ScanPlot::curveIndex(int id) const
{
    if (!m_series) return -1;
    if (m_series->ids.empty()) return id >= 0 && std::size_t(id) < m_series->y.size() ? id : -1;
    return m_idIndex.value(id, -1);
}

CurveStyle ScanPlot::resolvedStyle(std::size_t c) const
{
    CurveStyle s = m_overrides.value(curveId(c), m_defaultStyle);
    if (!s.color.isValid())
        s.color = m_series && c < m_series->colors.size() ? m_series->colors[c] : QColor(Qt::black);
    return s;
}

void ScanPlot::stylesEdited()
{
    m_dataDirty = m_styledDirty = true;
    ++m_styleRevision;
    emit stylesChanged();
    update();
}

void ScanPlot::setSelectedCurve(int id)
{
    if (id >= 0 && curveIndex(id) < 0) id = -1;
    if (id == m_selected) return;
    m_selected = id;
    stylesEdited();
}

bool ScanPlot::hasCustomStyles() const
{
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        if (curveIndex(it.key()) >= 0) return true;
    return false;
}

QVariantMap ScanPlot::curveInfo(int index) const
{
    if (!m_series || index < 0 || std::size_t(index) >= m_series->y.size()) return {};
    const std::size_t c = std::size_t(index);
    const CurveStyle s = resolvedStyle(c);
    const int id = curveId(c);
    return QVariantMap{{QStringLiteral("id"), id},
                       {QStringLiteral("label"), c < m_series->labels.size() ? m_series->labels[c] : tr("Curve %1").arg(c + 1)},
                       {QStringLiteral("color"), s.color},
                       {QStringLiteral("custom"), m_overrides.contains(id)},
                       {QStringLiteral("visible"), s.visible}};
}

QVariantMap ScanPlot::selectedStyle() const
{
    const int c = curveIndex(m_selected);
    if (c < 0) return {};
    QVariantMap m = resolvedStyle(std::size_t(c)).toMap();
    m.insert(QStringLiteral("custom"), m_overrides.contains(m_selected));
    return m;
}

void ScanPlot::setDefaultStyleMap(const QVariantMap& changes)
{
    CurveStyle s = m_defaultStyle.merged(changes);
    s.color = QColor();  // default curves always take the colormap colour
    if (s == m_defaultStyle) return;
    m_defaultStyle = s;
    stylesEdited();
}

void ScanPlot::setCurveStyles(const QHash<int, CurveStyle>& styles)
{
    if (styles == m_overrides) return;
    m_overrides = styles;
    stylesEdited();
}

void ScanPlot::setCurveStyle(int id, const QVariantMap& changes)
{
    const int c = curveIndex(id);
    if (c < 0) return;
    // A new individual style starts from what the curve looks like now, colour included.
    const CurveStyle base = m_overrides.value(id, resolvedStyle(std::size_t(c)));
    const CurveStyle s = base.merged(changes);
    if (m_overrides.contains(id) && m_overrides.value(id) == s) return;
    m_overrides.insert(id, s);
    stylesEdited();
}

void ScanPlot::resetCurveStyle(int id)
{
    if (m_overrides.remove(id)) stylesEdited();
}

void ScanPlot::resetCurveStyles()
{
    if (m_overrides.isEmpty()) return;
    m_overrides.clear();
    stylesEdited();
}

std::pair<std::size_t, std::size_t> ScanPlot::visibleRange(double x0, double x1) const
{
    const auto& x = m_series->x;
    if (m_xOrder == 0) return {0, x.size()};
    std::size_t a, b;
    if (m_xOrder > 0) {
        a = std::size_t(std::lower_bound(x.begin(), x.end(), float(x0)) - x.begin());
        b = std::size_t(std::upper_bound(x.begin(), x.end(), float(x1)) - x.begin());
    } else {
        a = std::size_t(std::lower_bound(x.begin(), x.end(), float(x1), std::greater<float>()) - x.begin());
        b = std::size_t(std::upper_bound(x.begin(), x.end(), float(x0), std::greater<float>()) - x.begin());
    }
    // One point beyond each edge so lines leave the plot instead of ending at its border.
    return {a > 0 ? a - 1 : 0, std::min(b + 1, x.size())};
}

int ScanPlot::curveAt(double px, double py) const
{
    if (!hasData()) return -1;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    constexpr double kTolerance = 12.0;  // finger (device-independent pixels)
#else
    constexpr double kTolerance = 5.0;
#endif
    const QPointF p(px, py);
    const auto& xs = m_series->x;
    int best = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (std::size_t c = 0; c < m_series->y.size(); ++c) {
        const CurveStyle s = resolvedStyle(c);
        if (!s.visible) continue;
        const double reach = kTolerance + std::max(double(s.width), s.marker ? double(s.markerSize) : 0.0) / 2;
        const auto [i0, i1] = visibleRange(toDataX(px - reach), toDataX(px + reach));
        const auto& ys = m_series->y[c];
        const std::size_t end = std::min(i1, ys.size());
        double d = std::numeric_limits<double>::max();
        for (std::size_t j = i0; j < end; ++j) {
            if (!std::isfinite(ys[j])) continue;
            const QPointF a(toPixelX(xs[j]), toPixelY(ys[j]));
            if (s.marker || s.line == CurveStyle::NoLine) d = std::min(d, std::hypot(a.x() - px, a.y() - py));
            if (s.line != CurveStyle::NoLine && j + 1 < end && std::isfinite(ys[j + 1]))
                d = std::min(d, distanceToSegment(p, a, QPointF(toPixelX(xs[j + 1]), toPixelY(ys[j + 1]))));
        }
        if (d <= reach && d < bestDist) {
            bestDist = d;
            best = curveId(c);
        }
    }
    return best;
}

void ScanPlot::buildStyledVertices()
{
    auto& out = m_styledVertices;
    out.clear();
    if (!hasData()) return;
    const auto& xs = m_series->x;
    const auto [i0, i1] = visibleRange(m_view.left(), m_view.right());

    std::vector<std::size_t> order;
    std::size_t selected = std::size_t(-1);
    for (std::size_t c = 0; c < m_series->y.size(); ++c) {
        const CurveStyle s = resolvedStyle(c);
        if (!s.visible) continue;
        if (curveId(c) == m_selected) selected = c;
        else if (!s.isPlain()) order.push_back(c);
    }
    if (selected != std::size_t(-1)) order.push_back(selected);  // selected on top

    std::vector<QPointF> pts;
    for (std::size_t c : order) {
        const CurveStyle s = resolvedStyle(c);
        const auto& ys = m_series->y[c];
        const std::size_t end = std::min(i1, ys.size());
        const bool isSelected = c == selected;
        const double w = std::max(1.0, double(s.width)) + (isSelected ? 1.0 : 0.0);

        // Pixel polyline; points closer than ~1 px to the previous one add nothing visible.
        // A NaN breaks the line (stored as a NaN point).
        pts.clear();
        QPointF last(std::numeric_limits<double>::quiet_NaN(), 0);
        for (std::size_t j = i0; j < end; ++j) {
            if (!std::isfinite(ys[j])) {
                if (!pts.empty() && std::isfinite(pts.back().x()))
                    pts.emplace_back(std::numeric_limits<double>::quiet_NaN(), 0);
                last.setX(std::numeric_limits<double>::quiet_NaN());
                continue;
            }
            const QPointF q(toPixelX(xs[j]), toPixelY(ys[j]));
            const bool runEnd = j + 1 == end || !std::isfinite(ys[j + 1]);
            if (std::isfinite(last.x()) && !runEnd
                && std::abs(q.x() - last.x()) + std::abs(q.y() - last.y()) < 0.75)
                continue;
            pts.push_back(q);
            last = q;
        }

        const bool hasLine = s.line != CurveStyle::NoLine;
        if (isSelected) {  // translucent halo below the selected curve
            TriangleWriter halo{out, Rgba::from(s.color, 0.4)};
            const double hw = w + 8;
            for (std::size_t k = 1; k < pts.size(); ++k)
                if (hasLine && std::isfinite(pts[k - 1].x()) && std::isfinite(pts[k].x()))
                    halo.segment(pts[k - 1], pts[k], hw, false);
            if (!hasLine || s.marker)
                for (const QPointF& q : pts)
                    if (std::isfinite(q.x())) halo.marker(CurveStyle::Circle, q, s.markerSize + 6);
        }

        TriangleWriter tw{out, Rgba::from(s.color)};
        if (hasLine) {
            Dasher dash(tw, w, s.line);
            for (std::size_t k = 1; k < pts.size(); ++k)
                if (std::isfinite(pts[k - 1].x()) && std::isfinite(pts[k].x()))
                    dash.segment(pts[k - 1], pts[k]);
        }
        if (s.marker) {
            QPointF lastMarker(std::numeric_limits<double>::quiet_NaN(), 0);
            const double minGap = 0.4 * s.markerSize;  // overlapping markers look alike
            for (const QPointF& q : pts) {
                if (!std::isfinite(q.x())) continue;
                if (std::isfinite(lastMarker.x()) && std::hypot(q.x() - lastMarker.x(), q.y() - lastMarker.y()) < minGap)
                    continue;
                tw.marker(s.marker, q, s.markerSize);
                lastMarker = q;
            }
        }
    }
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
        m_gridDirty = m_styledDirty = true;
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
        root->styled = new QSGNode;
        root->appendChildNode(root->styled);
        m_dataDirty = m_gridDirty = m_styledDirty = true;
    }

    // Curves: rebuilt only when the data changes.
    if (m_dataDirty) {
        m_dataDirty = false;
        while (QSGNode* child = root->transform->firstChild()) {
            root->transform->removeChildNode(child);
            delete child;
        }
        // Only plain curves go into the static buffer; styled ones are drawn in pixel space.
        std::vector<char> fast(m_series ? m_series->y.size() : 0);
        std::size_t total = 0;
        const std::size_t perCurve = m_series && m_series->x.size() > 1 ? 2 * (m_series->x.size() - 1) : 0;
        for (std::size_t c = 0; c < fast.size(); ++c) {
            fast[c] = resolvedStyle(c).isPlain() && curveId(c) != m_selected;
            if (fast[c]) total += perCurve;
        }
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
                if (!fast[c]) continue;
                const auto& ys = m_series->y[c];
                const QColor col = resolvedStyle(c).color;
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

    // Styled curves: pixel-space triangles for the visible range, rebuilt on any view change.
    // Chunk nodes are pooled (emptied, not removed) so the node structure stays stable.
    if (m_styledDirty) {
        m_styledDirty = false;
        buildStyledVertices();
        const std::size_t n = m_styledVertices.size();
        if (n || root->styledVertices) {
            root->styledVertices = n;
            std::size_t offset = 0;
            QSGNode* child = root->styled->firstChild();
            while (offset < n || child) {
                if (!child) {
                    child = makeCurveNode(0);
                    auto* g = static_cast<QSGGeometryNode*>(child)->geometry();
                    g->setDrawingMode(QSGGeometry::DrawTriangles);
                    g->setVertexDataPattern(QSGGeometry::DynamicPattern);
                    root->styled->appendChildNode(child);
                }
                auto* node = static_cast<QSGGeometryNode*>(child);
                const int count = int(std::min<std::size_t>(n - offset, kMaxTrianglesVertices));
                QSGGeometry* g = node->geometry();
                if (g->vertexCount() != count) g->allocate(count);
                if (count)
                    std::copy_n(m_styledVertices.data() + offset, count, g->vertexDataAsColoredPoint2D());
                node->markDirty(QSGNode::DirtyGeometry);
                offset += std::size_t(count);
                child = child->nextSibling();
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
