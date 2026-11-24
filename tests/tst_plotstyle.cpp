// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ScanPlot.h"

#include <QTest>

namespace {

PlotSeriesPtr makeTwoCurveSeries(QColor c0 = Qt::blue, QColor c1 = Qt::red)
{
    auto s = std::make_shared<PlotSeries>();
    s->x = {0.0f, 1.0f, 2.0f};
    s->y = {{1.0f, 2.0f, 3.0f}, {2.0f, 3.0f, 4.0f}};
    s->colors = {c0, c1};
    s->ids = {0, 1};
    s->labels = {QStringLiteral("measured"), QStringLiteral("fit")};
    s->computeBounds();
    return s;
}

}  // namespace

class TestPlotStyle : public QObject {
    Q_OBJECT
private slots:
    void baseStylesDoNotCountAsCustom()
    {
        ScanPlot plot;
        CurveStyle scatter;
        scatter.marker = CurveStyle::Circle;
        scatter.markerSize = 4.0f;
        scatter.line = CurveStyle::NoLine;

        CurveStyle dashed;
        dashed.line = CurveStyle::Dash;
        dashed.width = 2.0f;

        plot.initDefaultStyle(scatter);
        plot.setBaseCurveStyles({{1, dashed}});
        plot.setSeries(makeTwoCurveSeries());

        QVERIFY(!plot.hasCustomDefaultStyle());
        QVERIFY(!plot.hasCustomStyles());

        const QVariantMap c0 = plot.curveStyle(0);
        QCOMPARE(c0.value(QStringLiteral("marker")).toInt(), int(CurveStyle::Circle));
        QCOMPARE(c0.value(QStringLiteral("line")).toInt(), int(CurveStyle::NoLine));
        QCOMPARE(c0.value(QStringLiteral("color")).value<QColor>(), QColor(Qt::blue));
        QCOMPARE(c0.value(QStringLiteral("custom")).toBool(), false);

        const QVariantMap c1 = plot.curveStyle(1);
        QCOMPARE(c1.value(QStringLiteral("line")).toInt(), int(CurveStyle::Dash));
        QCOMPARE(c1.value(QStringLiteral("marker")).toInt(), int(CurveStyle::NoMarker));
        QCOMPARE(c1.value(QStringLiteral("color")).value<QColor>(), QColor(Qt::red));
        QCOMPARE(c1.value(QStringLiteral("custom")).toBool(), false);
    }

    void defaultChangesLayerOverBaseAndResetCleanly()
    {
        ScanPlot plot;
        CurveStyle scatter;
        scatter.marker = CurveStyle::Circle;
        scatter.markerSize = 4.0f;
        scatter.line = CurveStyle::NoLine;

        CurveStyle dashed;
        dashed.line = CurveStyle::Dash;
        dashed.width = 1.5f;

        plot.initDefaultStyle(scatter);
        plot.setBaseCurveStyles({{1, dashed}});
        plot.setSeries(makeTwoCurveSeries());

        // User edits "All curves" width and marker size -> layers over both base default and base per-curve styles.
        plot.setDefaultStyleMap({{QStringLiteral("width"), 3.0f}});
        QVERIFY(plot.hasCustomDefaultStyle());
        QVERIFY(!plot.hasCustomStyles());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("width")).toFloat(), 3.0f);
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("line")).toInt(), int(CurveStyle::NoLine));
        QCOMPARE(plot.curveStyle(1).value(QStringLiteral("width")).toFloat(), 3.0f);
        QCOMPARE(plot.curveStyle(1).value(QStringLiteral("line")).toInt(), int(CurveStyle::Dash));

        // Re-applying base styles (e.g. when controller refreshes data) must preserve user edits.
        plot.initDefaultStyle(scatter);
        plot.setBaseCurveStyles({{1, dashed}});
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("width")).toFloat(), 3.0f);
        QCOMPARE(plot.curveStyle(1).value(QStringLiteral("width")).toFloat(), 3.0f);

        // Resetting default style restores built-in scatter + dashed fit.
        plot.resetDefaultStyle();
        QVERIFY(!plot.hasCustomDefaultStyle());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("width")).toFloat(), 1.0f);
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("marker")).toInt(), int(CurveStyle::Circle));
        QCOMPARE(plot.curveStyle(1).value(QStringLiteral("width")).toFloat(), 1.5f);
        QCOMPARE(plot.curveStyle(1).value(QStringLiteral("line")).toInt(), int(CurveStyle::Dash));
    }

    void individualOverridePersistsAcrossBaseRefresh()
    {
        ScanPlot plot;
        CurveStyle scatter;
        scatter.marker = CurveStyle::Circle;
        scatter.markerSize = 4.0f;
        scatter.line = CurveStyle::NoLine;

        plot.initDefaultStyle(scatter);
        plot.setSeries(makeTwoCurveSeries());

        plot.setCurveStyle(0, {{QStringLiteral("color"), QColor(Qt::green)}, {QStringLiteral("marker"), int(CurveStyle::Diamond)}});
        QVERIFY(plot.hasCustomStyles());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("custom")).toBool(), true);
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("color")).value<QColor>(), QColor(Qt::green));
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("marker")).toInt(), int(CurveStyle::Diamond));

        // Refreshing series + base styles preserves the individual override.
        plot.initDefaultStyle(scatter);
        plot.setSeries(makeTwoCurveSeries());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("color")).value<QColor>(), QColor(Qt::green));
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("marker")).toInt(), int(CurveStyle::Diamond));

        // Reset restores the built-in style and original series colour.
        plot.resetCurveStyle(0);
        QVERIFY(!plot.hasCustomStyles());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("color")).value<QColor>(), QColor(Qt::blue));
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("marker")).toInt(), int(CurveStyle::Circle));
    }

    void individualOverrideKeepsOnlyChangedProperties()
    {
        ScanPlot plot;
        plot.setSeries(makeTwoCurveSeries());

        // Hiding a curve must not freeze its colour, width etc.
        plot.setCurveStyle(0, {{QStringLiteral("visible"), false}});
        QCOMPARE(plot.curveStyles().value(0), (QVariantMap{{QStringLiteral("visible"), false}}));
        plot.setDefaultStyleMap({{QStringLiteral("width"), 4.0f}});
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("width")).toFloat(), 4.0f);
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("visible")).toBool(), false);

        // The series colour follows data changes (e.g. another colormap).
        plot.setSeries(makeTwoCurveSeries(Qt::cyan, Qt::magenta));
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("color")).value<QColor>(), QColor(Qt::cyan));

        // Setting a property back to the inherited value drops the override.
        plot.setCurveStyle(0, {{QStringLiteral("visible"), true}});
        QVERIFY(!plot.hasCustomStyles());
        QCOMPARE(plot.curveStyle(0).value(QStringLiteral("custom")).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TestPlotStyle)
#include "tst_plotstyle.moc"
