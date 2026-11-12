// Offscreen screenshot harness for the README: loads the real window with a
// made-up page, grabs it, and exits. Never touches the user's screen.

#include <QDir>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "backend.h"
#include "hyprland.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Runs as a real window by default so WebEngine composites through the
    // real GPU; pass GRAB_OFFSCREEN=1 for the headless software attempt.
    if (qEnvironmentVariableIsSet("GRAB_OFFSCREEN"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    // Same app id as pique, so the peek float rule applies and the shot is
    // the window size rather than a tiled workspace.
    app.setDesktopFileName(QStringLiteral("pique"));
    app.setApplicationName(QStringLiteral("pique"));
    QQuickStyle::setStyle("Material");

    // Default accent and English: the README picture must not depend on the
    // machine it was taken on. PIQUE_GRAB_LANG points at a language file
    // when checking a translation.
    const QString langFile = qEnvironmentVariable("PIQUE_GRAB_LANG");
    Translator translator(langFile.isEmpty() ? QDir::tempPath() + "/pique-readme/no-language" : langFile);
    app.installTranslator(&translator);
    Theme theme(QDir::tempPath() + "/pique-readme/no-theme");
    Backend backend;
    Hyprland hyprland;
    backend.setInitialUrl(argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("example.com"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("hyprland", &hyprland);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qWarning("grab: the window failed to load");
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (qEnvironmentVariableIsSet("PIQUE_GRAB_HELP"))
        window->setProperty("helpVisible", true);
    window->resize(780, 600);
    window->show();
    QTimer::singleShot(12000, [window] {
        qInfo("grab: window title: %s", qPrintable(window->title()));
        const QString hover = qEnvironmentVariable("PIQUE_GRAB_HOVER");
        if (!hover.isEmpty()) {
            if (auto *button = window->findChild<QQuickItem *>(hover)) {
                const QPointF scene = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
                QHoverEvent enter(QEvent::HoverEnter, scene, scene, QPointF(-1, -1));
                QCoreApplication::sendEvent(window, &enter);
            }
        }
        QQuickWindow *dock = nullptr;
        for (QWindow *candidate : QGuiApplication::allWindows()) {
            if (candidate->title() == QLatin1String("pique-tools"))
                dock = qobject_cast<QQuickWindow *>(candidate);
        }
        QImage page = window->grabWindow();
        QImage tools = dock ? dock->grabWindow() : QImage();
        if (!tools.isNull()) {
            int topRow = tools.height();
            for (int y = 0; y < tools.height() && topRow == tools.height(); ++y) {
                for (int x = 0; x < tools.width(); ++x) {
                    if (qAlpha(tools.pixel(x, y)) > 12) {
                        topRow = y;
                        break;
                    }
                }
            }
            if (topRow < tools.height())
                tools = tools.copy(0, qMax(0, topRow - 6), tools.width(), tools.height() - qMax(0, topRow - 6));
        }
        const int gap = tools.isNull() ? 0 : 8;
        const int top = tools.isNull() ? 0 : tools.height() + gap;
        QImage shot(page.width(), page.height() + top, QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(QStringLiteral("#101014")));
        QPainter painter(&shot);
        if (!tools.isNull())
            painter.drawImage(page.width() - tools.width() - 8, 0, tools);
        painter.drawImage(0, top, page);
        painter.end();
        // Into the current directory; copy it to docs/screenshot.png by hand.
        shot.save(QStringLiteral("screenshot.png"));
        std::exit(0);
    });
    return app.exec();
}
