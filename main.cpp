#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWindow>
#include <csignal>
#include <cstdio>

static volatile std::sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

int main(int argc, char **argv)
{
    // XWayland supports keep-above and placement, which ordinary Wayland windows cannot request.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    app.setApplicationName("foverlay");
    app.setApplicationVersion("1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Transparent, borderless, click-through URL overlay. Stop with Ctrl+C.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("url", "URL or local HTML file to display.");
    parser.addOption({"screen", "Monitor index, starting at 0.", "index", "0"});
    parser.addOption({"list-screens", "List monitors and exit."});
    parser.addOption({"keep-background", "Preserve backgrounds explicitly drawn by the page."});
    parser.addOption({"smoke-test", "Check rendering and exit automatically."});
    parser.process(app);
    const auto screens = app.screens();
    if (parser.isSet("list-screens")) {
        for (int i = 0; i < screens.size(); ++i) {
            const auto r = screens[i]->geometry();
            std::printf("%d: %s (%dx%d at %d,%d)\n", i, qPrintable(screens[i]->name()), r.width(), r.height(), r.x(), r.y());
        }
        return 0;
    }
    bool valid = false;
    const int index = parser.value("screen").toInt(&valid);
    if (!valid || index < 0 || index >= screens.size()) {
        std::fprintf(stderr, "Invalid monitor index. Use --list-screens.\n");
        return 1;
    }
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(1);
    const QString input = parser.positionalArguments().first();
    const QUrl url = QFileInfo::exists(input)
        ? QUrl::fromLocalFile(QFileInfo(input).absoluteFilePath())
        : QUrl::fromUserInput(input);
    if (!url.isValid() || url.isEmpty()) {
        std::fprintf(stderr, "Invalid URL.\n");
        return 1;
    }

    QWebEngineView view;
    view.setWindowTitle("foverlay");
    view.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                        | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
                        | Qt::NoDropShadowWindowHint | Qt::Tool);
    view.setAttribute(Qt::WA_TranslucentBackground);
    view.setAttribute(Qt::WA_ShowWithoutActivating);
    view.setContextMenuPolicy(Qt::NoContextMenu);
    view.page()->setBackgroundColor(Qt::transparent);
    view.settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    view.settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    if (!parser.isSet("keep-background")) {
        QWebEngineScript script;
        script.setName("transparent-page");
        script.setInjectionPoint(QWebEngineScript::DocumentCreation);
        script.setWorldId(QWebEngineScript::ApplicationWorld);
        script.setSourceCode(R"JS(
            (() => {
                const apply = () => {
                    const style = document.createElement('style');
                    style.textContent = 'html,body{background:transparent!important;background-image:none!important}';
                    (document.head || document.documentElement).appendChild(style);
                };
                if (document.documentElement) apply();
                else document.addEventListener('DOMContentLoaded', apply, {once:true});
            })();
        )JS");
        view.page()->scripts().insert(script);
    }
    view.winId();
    view.windowHandle()->setScreen(screens[index]);
    view.setGeometry(screens[index]->geometry());
    view.show();
    QObject::connect(screens[index], &QScreen::geometryChanged, &view, [&view](const QRect &r) { view.setGeometry(r); });
    QObject::connect(view.page(), &QWebEnginePage::renderProcessTerminated, &app,
                     [&app](QWebEnginePage::RenderProcessTerminationStatus, int code) {
        std::fprintf(stderr, "Browser process stopped (%d).\n", code);
        app.exit(1);
    });
    QObject::connect(&view, &QWebEngineView::loadFinished, &app, [&](bool ok) {
        if (!ok) std::fprintf(stderr, "Page failed to load.\n");
        if (!parser.isSet("smoke-test")) return;
        if (!ok) { app.exit(1); return; }
        QTimer::singleShot(1500, &view, [&] {
            const auto image = view.grab().toImage();
            const bool transparent = image.hasAlphaChannel() && image.pixelColor(image.width()-1, image.height()-1).alpha() == 0;
            const bool content = image.pixelColor(20, 20).red() > 200 && image.pixelColor(20, 20).alpha() > 200;
            const bool geometry = view.geometry() == screens[index]->geometry();
            std::printf("Transparent corner: %s; rendered content: %s; monitor coverage: %s\n",
                        transparent ? "PASS" : "FAIL", content ? "PASS" : "FAIL", geometry ? "PASS" : "FAIL");
            app.exit(transparent && content && geometry ? 0 : 1);
        });
    });
    if (parser.isSet("smoke-test")) QTimer::singleShot(20000, &app, [&app] { app.exit(1); });
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    QTimer signalTimer;
    QObject::connect(&signalTimer, &QTimer::timeout, &app, [&app] { if (stopping) app.quit(); });
    signalTimer.start(200);
    view.load(url);
    return app.exec();
}
