// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppController.h"
#include "ScanPlot.h"

#include "auc/AucFile.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include <cmath>
#include <cstdio>
#include <random>

namespace {

/// Builds a synthetic series of `scans` curves × `points` for the render benchmark.
PlotSeriesPtr benchmarkSeries(int scans, int points)
{
    auto s = std::make_shared<PlotSeries>();
    s->x.resize(size_t(points));
    for (int j = 0; j < points; ++j) s->x[size_t(j)] = float(5.9 + 1.3 * j / (points - 1));
    std::mt19937 rng(1);
    std::normal_distribution<float> g(0.f, 0.004f);
    for (int i = 0; i < scans; ++i) {
        const double rb = 6.0 + 1.0 * i / scans;
        std::vector<float> y(static_cast<size_t>(points));
        for (int j = 0; j < points; ++j)
            y[size_t(j)] = float(0.8 * 0.5 * std::erfc((rb - s->x[size_t(j)]) / 0.04)) + g(rng);
        s->y.push_back(std::move(y));
        s->colors.push_back(Colormap::color(Colormap::Viridis, double(i) / std::max(scans - 1, 1)));
    }
    s->computeBounds();
    return s;
}

#ifdef Q_OS_ANDROID
/// A run is a folder of scan files plus run XML; reading it needs file system access to
/// shared storage, which a document picker grant (single URIs) does not give.
/// Android 11+: "All files access" settings page; Android 9/10: storage runtime permission.
void requestStorageAccess()
{
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) return;
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        if (QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager")) return;
        const QJniObject pkg = activity.callObjectMethod("getPackageName", "()Ljava/lang/String;");
        const QJniObject uri = QJniObject::callStaticObjectMethod(
            "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
            QJniObject::fromString(QStringLiteral("package:") + pkg.toString()).object<jstring>());
        const QJniObject action = QJniObject::getStaticObjectField<jstring>(
            "android/provider/Settings", "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
        const QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
                                action.object<jstring>(), uri.object());
        activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    } else {
        QJniEnvironment env;
        const QJniObject perm = QJniObject::fromString(QStringLiteral("android.permission.READ_EXTERNAL_STORAGE"));
        jobjectArray perms = env->NewObjectArray(1, env.findClass("java/lang/String"), perm.object<jstring>());
        activity.callMethod<void>("requestPermissions", "([Ljava/lang/String;I)V", perms, jint(0));
        env->DeleteLocalRef(perms);
    }
    QJniEnvironment().checkAndClearExceptions();
}
#endif

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("AG Coelfen"));
    app.setApplicationName(QStringLiteral("AUCDataTool"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Viewer for analytical ultracentrifugation data"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral(".auc files or folders to open"), QStringLiteral("[files...]"));
    const QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a window screenshot and quit."), QStringLiteral("png"));
    const QCommandLineOption benchOpt(QStringLiteral("benchmark"),
                                      QStringLiteral("Render N synthetic scans, report frame times and quit."), QStringLiteral("scans"));
    const QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Window size WxH."), QStringLiteral("WxH"), QStringLiteral("1400x860"));
    const QCommandLineOption setOpt(QStringLiteral("set"),
                                    QStringLiteral("Set a processing option, e.g. --set integrate=true (repeatable)."),
                                    QStringLiteral("key=value"));
    const QCommandLineOption tiOpt(QStringLiteral("ti-noise"), QStringLiteral("TI noise file for the first data file."), QStringLiteral("file"));
    const QCommandLineOption riOpt(QStringLiteral("ri-noise"), QStringLiteral("RI noise file for the first data file."), QStringLiteral("file"));
    const QCommandLineOption watchOpt(QStringLiteral("watch"), QStringLiteral("Open a folder and follow new scans (live mode)."), QStringLiteral("folder"));
    const QCommandLineOption delayOpt(QStringLiteral("screenshot-delay"), QStringLiteral("Delay before --screenshot (ms, default 2500)."), QStringLiteral("ms"), QStringLiteral("2500"));
    parser.addOptions({shotOpt, benchOpt, sizeOpt, setOpt, tiOpt, riOpt, watchOpt, delayOpt});
    parser.process(app);

#ifdef Q_OS_ANDROID
    QQuickStyle::setStyle(QStringLiteral("Material"));
    requestStorageAccess();
#else
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) QQuickStyle::setStyle(QStringLiteral("Fusion"));
#endif

    QQmlApplicationEngine engine;
    const QStringList size = parser.value(sizeOpt).split(QLatin1Char('x'));
    engine.setInitialProperties({{QStringLiteral("width"), size.value(0).toInt()},
                                 {QStringLiteral("height"), size.value(1).toInt()}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Auc.DataTool", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* controller = window->property("controller").value<AppController*>();
    if (controller) {
        controller->openPaths(parser.positionalArguments());
        if (parser.isSet(watchOpt)) controller->openFolder(QUrl::fromLocalFile(parser.value(watchOpt)), true);
        if (parser.isSet(tiOpt)) controller->loadNoise(QUrl::fromLocalFile(parser.value(tiOpt)), true);
        if (parser.isSet(riOpt)) controller->loadNoise(QUrl::fromLocalFile(parser.value(riOpt)), false);
        for (const QString& kv : parser.values(setOpt)) {
            const qsizetype eq = kv.indexOf(QLatin1Char('='));
            if (eq <= 0 || !controller->setProperty(kv.left(eq).toUtf8().constData(), kv.mid(eq + 1)))
                std::fprintf(stderr, "unknown option: %s\n", qPrintable(kv));
        }
    }

    if (parser.isSet(benchOpt)) {
        // Measures upload of N scans and the cost of zoom frames (transform-only updates).
        auto* plot = window->property("mainPlot").value<ScanPlot*>();
        if (!plot) return 1;
        const int scans = parser.value(benchOpt).toInt();
        const int points = 1000;
        auto series = benchmarkSeries(scans, points);
        auto* frames = new QList<qint64>;
        auto* timer = new QElapsedTimer;
        auto* phase = new int(0);
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [=]() {
            const qint64 ns = timer->nsecsElapsed();
            if (*phase == 1) {
                std::printf("upload+first frame: %d scans x %d points (%lld vertices): %.1f ms\n", scans, points,
                            static_cast<long long>(series->vertexCount()), ns / 1e6);
                *phase = 2;
            } else if (*phase >= 2) {
                frames->append(ns);
                if (frames->size() == 120) {
                    QList<qint64> sorted = *frames;
                    std::sort(sorted.begin(), sorted.end());
                    std::printf("zoom frames (n=120): median %.2f ms, p95 %.2f ms\n", sorted[60] / 1e6, sorted[114] / 1e6);
                    QCoreApplication::quit();
                    return;
                }
                plot->zoomAt(plot->width() / 2, plot->height() / 2, frames->size() % 40 < 20 ? 1.02 : 1 / 1.02, 1.0);
            }
            timer->restart();
            window->update();
        });
        QTimer::singleShot(500, window, [=]() {
            *phase = 1;
            timer->start();
            plot->setSeries(series);
        });
    }

    if (parser.isSet(shotOpt)) {
        const QString out = parser.value(shotOpt);
        QTimer::singleShot(parser.value(delayOpt).toInt(), window, [window, out]() {
            const QImage img = window->grabWindow();
            std::printf("screenshot %s: %s\n", qPrintable(out), img.save(out) ? "ok" : "FAILED");
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
