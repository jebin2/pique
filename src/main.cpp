// pique — the peek browser: look at a link without paying for it with a tab.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "backend.h"
#include "hyprland.h"
#include "standby.h"

#include <utility>
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromUtf8(argv[i]);
    // Launchers pass the address or highlighted text here instead of on the
    // command line, which every account on the machine can read
    // (/proc/PID/cmdline); the environment is private to this user. Cleared
    // before the web engine starts its helper processes.
    args << Standby::takeEnvironmentArgs();
    Launch launch = Standby::parse(args);
    if (launch.help) {
        qInfo("pique — the peek browser\n\nusage: pique [address or search text]\n"
              "       pique --prefill=TEXT\n"
              "       pique --standby | --stop-standby\n\n"
              "Each launch opens its own window. An empty launch shows the search\n"
              "bar; --prefill puts TEXT in it, selected, without searching yet.\n"
              "--standby starts hidden and waits for the next launch, so it opens\n"
              "at once (one spare at a time); --stop-standby makes it quit.");
        return 0;
    }
    if (launch.version) {
        qInfo("pique 0.1.1");
        return 0;
    }
    if (launch.stopStandby) {
        QCoreApplication app(argc, argv);
        Standby::stopSpare();
        return 0;
    }
    // A spare started while the compositor is still coming up (Hyprland runs
    // its config, and so our exec, before its socket listens) would make Qt
    // abort and dump core. Exit quietly: the hyprland.start hook starts the
    // real one a moment later.
    if (launch.standby && !Standby::displayReady()) {
        qInfo("pique: display not ready yet, spare not started");
        return 0;
    }
    // Before anything slow: a second spare exits at once.
    Standby standby;
    if (launch.standby && !standby.takeLock())
        return 0;

    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    app.setApplicationName("pique");
    app.setApplicationVersion("0.1.1");

    // Matches the window to pique.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("pique");
    app.setWindowIcon(QIcon::fromTheme("pique"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;
    Hyprland hyprland;
    backend.setStandby(launch.standby);
    backend.setInitialUrl(launch.url);
    backend.setInitialSearch(launch.prefill);
    app.installEventFilter(&backend);
    QObject::connect(&hyprland, &Hyprland::failed, &backend, &Backend::note);

    // Every launch is its own window. A second peek must not navigate one
    // that is already open.
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("hyprland", &hyprland);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // The next spare starts once this window is up, so it doesn't slow the
    // page down; a peek closed sooner than that starts it as it quits.
    // A spare told to quit (upgrade, removal) never arms this.
    bool spareArmed = false;
    const auto armSpare = [&spareArmed] {
        spareArmed = true;
        QTimer::singleShot(1500, [&spareArmed] {
            if (std::exchange(spareArmed, false))
                Standby::ensureSpare();
        });
    };
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&spareArmed] {
        if (std::exchange(spareArmed, false))
            Standby::ensureSpare();
    });
    if (launch.standby) {
        QObject::connect(&standby, &Standby::woke, &backend, [&backend, &armSpare](const QStringList &woken) {
            const Launch next = Standby::parse(woken);
            backend.wake(next.url, next.prefill);
            armSpare();
        });
        if (!standby.listen())
            return 1;
        // Once settled, a waiting spare hands its memory to the kernel to
        // compress into swap (zram here: about 31 MB becomes 7). Waking reads
        // it back in a few milliseconds. Without swap this only drops pages
        // that are already on disk.
        QTimer::singleShot(3000, &backend, [&backend] {
            if (backend.standby())  // not if a launch woke it meanwhile
                Standby::pageOut();
        });
    } else {
        armSpare();
    }

    return app.exec();
}
