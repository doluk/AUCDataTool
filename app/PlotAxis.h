// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <cmath>
#include <limits>

/// Scale and limits of one plot axis, edited in the axis settings (AxisSettings.qml).
///
/// `min`/`max` are the limits currently shown, set by the owning plot (ScanPlot: the view,
/// SurfaceFeeder: the axis range). A locked limit is kept by autoscaling at its stored
/// value (`minLimit`/`maxLimit`); typing a limit locks it. Locking takes the current one.
class PlotAxis : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a plot")
    Q_PROPERTY(bool log READ log WRITE setLog NOTIFY logChanged)
    Q_PROPERTY(double min READ min NOTIFY rangeChanged)
    Q_PROPERTY(double max READ max NOTIFY rangeChanged)
    Q_PROPERTY(bool minLocked READ minLocked WRITE setMinLocked NOTIFY locksChanged)
    Q_PROPERTY(bool maxLocked READ maxLocked WRITE setMaxLocked NOTIFY locksChanged)
    Q_PROPERTY(double minLimit READ minLimit NOTIFY locksChanged)
    Q_PROPERTY(double maxLimit READ maxLimit NOTIFY locksChanged)

public:
    using QObject::QObject;

    bool log() const { return m_log; }
    void setLog(bool on)
    {
        if (on == m_log) return;
        m_log = on;
        // A logarithmic axis has no limits ≤ 0.
        if (on && m_minLocked && !(m_minLimit > 0)) m_minLocked = false;
        if (on && m_maxLocked && !(m_maxLimit > 0)) m_maxLocked = false;
        emit logChanged();
        emit locksChanged();
    }

    double min() const { return m_min; }
    double max() const { return m_max; }
    /// Called by the owner when the shown limits change.
    void setRange(double lo, double hi)
    {
        if (lo == m_min && hi == m_max) return;
        m_min = lo;
        m_max = hi;
        emit rangeChanged();
    }

    bool minLocked() const { return m_minLocked; }
    bool maxLocked() const { return m_maxLocked; }
    void setMinLocked(bool on) { setLocked(false, on); }
    void setMaxLocked(bool on) { setLocked(true, on); }
    double minLimit() const { return m_minLimit; }
    double maxLimit() const { return m_maxLimit; }

    /// Locks the lower (`upper` false) or upper limit at `value`. Returns false (and
    /// changes nothing) for a value that does not fit the axis: ≤ 0 on a log axis, or not
    /// below/above the other limit.
    Q_INVOKABLE bool setLimit(bool upper, double value)
    {
        if (!std::isfinite(value) || (m_log && !(value > 0))) return reject();
        const double other = upper ? (m_minLocked ? m_minLimit : m_min) : (m_maxLocked ? m_maxLimit : m_max);
        if (upper ? !(value > other) : !(value < other)) return reject();
        (upper ? m_maxLimit : m_minLimit) = value;
        (upper ? m_maxLocked : m_minLocked) = true;
        emit locksChanged();
        emit limitEdited(upper);
        return true;
    }

    /// Lower/upper limit to use when autoscaling: the locked value, else `dataValue`.
    double limitOr(bool upper, double dataValue) const
    {
        if (upper ? m_maxLocked : m_minLocked) return upper ? m_maxLimit : m_minLimit;
        return dataValue;
    }

signals:
    void logChanged();
    void rangeChanged();
    void locksChanged();
    /// A limit was typed in (already locked): the owner shows it.
    void limitEdited(bool upper);

private:
    void setLocked(bool upper, bool on)
    {
        bool& locked = upper ? m_maxLocked : m_minLocked;
        if (on == locked) return;
        locked = on;
        if (on) (upper ? m_maxLimit : m_minLimit) = upper ? m_max : m_min;
        emit locksChanged();
    }
    bool reject()
    {
        emit rangeChanged();  // the editor shows the current limit again
        return false;
    }

    bool m_log = false;
    double m_min = 0, m_max = 1;
    bool m_minLocked = false, m_maxLocked = false;
    double m_minLimit = 0, m_maxLimit = 1;
};
