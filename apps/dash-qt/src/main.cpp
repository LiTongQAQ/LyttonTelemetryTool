// Lytton Telemetry Tool, Qt desktop window. The engine (telemetry source, HMD panels, SteamVR) runs on the GUI
// thread from a timer; QML gets its data from the Backend (`Race` singleton) and draws the pages.
//
//   lytton [--port 20700] [--replay file.f1tr] [--demo] [--page N]
#include "backend.hpp"
#include "carsvg.hpp"
#include "engine.hpp"
#include "log.hpp"
#include "single.hpp"
#include "imcanvas.hpp"
#include "trackmap.hpp"
#include "ui.hpp"

#include <QScreen>

#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>

#include <filesystem>

// Qt / QML warnings into the program log (a WIN32 exe has no console)
static void qtMessage(QtMsgType t, const QMessageLogContext &, const QString &msg)
{
    static const char *kind[] = {"debug", "warning", "critical", "fatal", "info"};
    LOG("qt %s: %s", t <= 4 ? kind[t] : "?", msg.toUtf8().constData());
}

int main(int argc, char *argv[])
{
    if (!single::acquire()) return 0; // already running: its window came to the front
    // full speed while the game has the focus: Windows would otherwise throttle a background program (EcoQoS)
    {
        PROCESS_POWER_THROTTLING_STATE pt{};
        pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        pt.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
    }
    QGuiApplication qapp(argc, argv);
    QGuiApplication::setApplicationName("Lytton Telemetry Tool");
    QGuiApplication::setWindowIcon(QIcon(":/assets/logo.png"));
    QQuickStyle::setStyle("Basic");

    const std::string dir = engine::dataDir();
    applog::open(dir);
    qInstallMessageHandler(qtMessage);
    LOG("lytton %s start (Qt %s), exe dir %s, data dir %s",
#ifdef F1RACE_VERSION
        F1RACE_VERSION,
#else
        "dev",
#endif
        qVersion(), engine::exeDir().c_str(), dir.c_str());

    app::App a(dir);
    std::string replayFile;
    bool demo = false;
    int page = 0;
    QString shot; // development: save a screenshot of the window after 4 s and quit
    const QStringList args = qapp.arguments();
    for (int i = 1; i < args.size(); i++) {
        const QString arg = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : QString(); };
        if (arg == "--port") a.cfg.port = uint16_t(next().toUInt());
        else if (arg == "--replay") replayFile = std::filesystem::path(next().toStdWString()).string();
        else if (arg == "--demo") demo = true;
        else if (arg == "--page") page = next().toInt();
        else if (arg == "--shot") shot = next();
    }
    if (!replayFile.empty()) a.useReplay(replayFile);
    else if (demo || a.cfg.startSource == 1) a.useSim();
    else a.useLive();
    a.scanRecordings();
    timing::setMyCode(a.cfg.myCode);

    engine::Engine eng(a);
    eng.init();
    TrackMap::engine = &eng;
    ui::setPage(page);
    // fonts rasterised at the scale the design (1920 x 1009) is shown at on this screen
    {
        const QScreen *scr = QGuiApplication::primaryScreen();
        const QSizeF av = scr ? QSizeF(scr->availableGeometry().size()) : QSizeF(1920, 1009);
        const double k = std::min(av.width() / 1920.0, av.height() / 1009.0) * (scr ? scr->devicePixelRatio() : 1.0);
        ImCanvas::setup(&eng, float(k));
    }
    if (!carsvg::install(&eng)) LOG("car SVG not found in the resources: the car is drawn from the polylines");

    Backend backend(eng, page);
    qmlRegisterSingletonInstance("F1Data", 1, 0, "Race", &backend);
    qmlRegisterType<TrackMap>("F1Native", 1, 0, "TrackMap");
    qmlRegisterType<ImCanvas>("F1Native", 1, 0, "ImCanvas");

    // engine at ~60 Hz (HMD texture), QML data at ~30 Hz
    QElapsedTimer clock;
    clock.start();
    int frame = 0;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        const float dt = float(clock.restart()) / 1000.f;
        eng.tick(dt);
        ++frame;
        backend.refresh();
        ImCanvas::tickAll(); // the desktop at 60 Hz, like the old window

    });
    timer.start(16);

    QQmlApplicationEngine qml;
    qml.addImportPath("qrc:/imports");
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &qapp, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    qml.load(QUrl("qrc:/content/App.qml"));
    if (!shot.isEmpty() && !qml.rootObjects().isEmpty())
        if (auto *w = qobject_cast<QQuickWindow *>(qml.rootObjects().first())) {
            w->setVisibility(QWindow::Windowed);
            w->resize(1920, 1009);
            QTimer::singleShot(4000, w, [w, shot] { w->grabWindow().save(shot); QCoreApplication::quit(); });
        }
    const int rc = qapp.exec();
    carsvg::shutdown();
    timer.stop();
    LOG("exit");
    return rc;
}
