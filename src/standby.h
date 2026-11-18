#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;

// A peek costs about a second to start (Qt, QML, the web engine). So one
// pique always waits hidden, already started: a launcher hands it the
// arguments over $XDG_RUNTIME_DIR/pique-standby and it only has to show its
// window. Then it starts the next spare. Without a spare, a launch is an
// ordinary cold start.
struct Launch {
    QString url;
    QString prefill;
    bool standby = false;
    bool help = false;
    bool version = false;
    bool stopStandby = false;
};

class Standby : public QObject {
    Q_OBJECT

public:
    explicit Standby(QObject *parent = nullptr) : QObject(parent) {}
    ~Standby() override;

    // The command line: the positional argument is an address or search
    // text (the last one wins); --prefill=TEXT; --standby.
    static Launch parse(const QStringList &args);
    // Arguments a launcher put in PIQUE_LAUNCH (a JSON list), removed from
    // the environment.
    static QStringList takeEnvironmentArgs();

    // Opens a peek: hands args to the waiting spare, or starts a new process.
    static bool launch(const QStringList &args);
    // Asks the waiting spare to quit (before an upgrade or removal).
    static bool stopSpare();
    // Starts a spare if none is waiting (or starting).
    static void ensureSpare();
    // Asks the kernel to swap out this whole process (MADV_PAGEOUT).
    static void pageOut();
    // False when $WAYLAND_DISPLAY names a socket nobody is listening on yet
    // (the compositor is still starting). Qt aborts in that case, so a spare
    // checks first and exits quietly. True when Wayland is not in use.
    static bool displayReady();

    // Only one spare at a time: false if another one holds the lock.
    bool takeLock();
    // Waits for a launcher. Emits woke() once, with its arguments.
    bool listen();

signals:
    void woke(const QStringList &args);

private:
    static QString path(const char *name);
    static bool hand(const QStringList &args);
    void accept();
    void releaseLock();

    QLocalServer *m_server = nullptr;
    int m_lock = -1;
    bool m_woken = false;
};
