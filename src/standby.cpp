#include "standby.h"
#include <QProcessEnvironment>

#include <sys/mman.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>

#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr qint64 kMaxMessage = 64 * 1024;
}

Standby::~Standby() {
    releaseLock();
}

Launch Standby::parse(const QStringList &args) {
    Launch launch;
    for (const QString &argument : args) {
        if (argument == QLatin1String("-h") || argument == QLatin1String("--help"))
            launch.help = true;
        else if (argument == QLatin1String("--version"))
            launch.version = true;
        else if (argument == QLatin1String("--standby"))
            launch.standby = true;
        else if (argument == QLatin1String("--stop-standby"))
            launch.stopStandby = true;
        else if (argument.startsWith(QLatin1String("--prefill=")))
            launch.prefill = argument.mid(10).trimmed().left(2000);
        else if (!argument.startsWith(QLatin1Char('-')))
            launch.url = argument;
    }
    return launch;
}

// Only in $XDG_RUNTIME_DIR: it is private to the user (0700). Without it
// there is no spare, and every launch is a cold start.
QString Standby::path(const char *name) {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty() || !QDir(runtime).exists())
        return {};
    return runtime + QLatin1Char('/') + QLatin1String(name);
}

bool Standby::hand(const QStringList &args) {
    const QString socketPath = path("pique-standby");
    if (socketPath.isEmpty())
        return false;
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(300))
        return false;
    socket.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(300))
        return false;
    // A spare that was just taken by another launch closes the connection.
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(1500))
            return false;
    }
    return socket.readLine().trimmed() == "ok";
}

bool Standby::stopSpare() {
    return hand({QStringLiteral("--stop-standby")});
}

bool Standby::launch(const QStringList &args) {
    if (hand(args))
        return true;
    // Not on the command line: see takeEnvironmentArgs().
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PIQUE_LAUNCH"),
               QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact)));
    process.setProcessEnvironment(env);
    process.setStandardInputFile(QProcess::nullDevice());
    return process.startDetached();
}

QStringList Standby::takeEnvironmentArgs() {
    const QByteArray raw = qgetenv("PIQUE_LAUNCH");
    qunsetenv("PIQUE_LAUNCH");
    QStringList args;
    if (raw.isEmpty() || raw.size() > kMaxMessage)
        return args;
    for (const QJsonValue &value : QJsonDocument::fromJson(raw).array()) {
        // A launch never asks for --standby or --stop-standby this way.
        if (value.isString() && !value.toString().startsWith(QLatin1String("--standby"))
            && value.toString() != QLatin1String("--stop-standby"))
            args << value.toString();
    }
    return args;
}

void Standby::pageOut() {
    QFile maps(QStringLiteral("/proc/self/maps"));
    if (!maps.open(QIODevice::ReadOnly))
        return;
    for (const QByteArray &line : maps.readAll().split('\n')) {
        // [vvar], [vdso], [vsyscall] belong to the kernel.
        if (line.isEmpty() || line.contains(" [v"))
            continue;
        const QList<QByteArray> range = line.left(line.indexOf(' ')).split('-');
        bool okFrom = false, okTo = false;
        const quintptr from = range.value(0).toULongLong(&okFrom, 16);
        const quintptr to = range.value(1).toULongLong(&okTo, 16);
        if (okFrom && okTo && to > from)
            ::madvise(reinterpret_cast<void *>(from), to - from, MADV_PAGEOUT);
    }
}

bool Standby::displayReady() {
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (!platform.isEmpty() && !platform.startsWith("wayland"))
        return true;
    const QByteArray display = qgetenv("WAYLAND_DISPLAY");
    if (display.isEmpty())
        return true;  // not a Wayland session: let Qt decide
    QByteArray socketPath = display;
    if (!display.startsWith('/')) {
        const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
        if (runtime.isEmpty())
            return true;
        socketPath = runtime + '/' + display;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socketPath.size() >= qsizetype(sizeof(address.sun_path)))
        return true;
    std::memcpy(address.sun_path, socketPath.constData(), socketPath.size());
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return true;
    const bool ready = ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    ::close(fd);
    return ready;
}

void Standby::ensureSpare() {
    const QString socketPath = path("pique-standby");
    if (socketPath.isEmpty())
        return;
    QLocalSocket probe;
    probe.connectToServer(socketPath);
    if (probe.waitForConnected(200)) {
        probe.abort();  // a spare is waiting
        return;
    }
    // If one is still starting, the new one finds the lock taken and exits.
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--standby")});
}

bool Standby::takeLock() {
    const QString lockPath = path("pique-standby.lock");
    if (lockPath.isEmpty())
        return false;
    // O_CLOEXEC: the next spare, started from this one, must not inherit it.
    m_lock = ::open(QFile::encodeName(lockPath).constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_lock < 0)
        return false;
    if (::flock(m_lock, LOCK_EX | LOCK_NB) != 0) {
        releaseLock();
        return false;
    }
    return true;
}

void Standby::releaseLock() {
    if (m_lock >= 0) {
        ::close(m_lock);
        m_lock = -1;
    }
}

bool Standby::listen() {
    const QString socketPath = path("pique-standby");
    if (socketPath.isEmpty())
        return false;
    // Holding the lock means any socket left there is from a spare that died.
    QLocalServer::removeServer(socketPath);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &Standby::accept);
    return m_server->listen(socketPath);
}

void Standby::accept() {
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (socket->bytesAvailable() > kMaxMessage) {
                socket->abort();
                return;
            }
            if (!socket->canReadLine())
                return;
            const QJsonDocument doc = QJsonDocument::fromJson(socket->readLine());
            if (m_woken || !doc.isArray()) {
                socket->abort();
                return;
            }
            QStringList args;
            for (const QJsonValue &value : doc.array()) {
                if (value.isString() && value.toString() != QLatin1String("--standby"))
                    args << value.toString();
            }
            m_woken = true;
            // pique-hypr (upgrade, removal): only a spare that is still
            // waiting quits; open peeks stay.
            if (args == QStringList{QStringLiteral("--stop-standby")}) {
                socket->write("ok\n");
                socket->flush();
                socket->disconnectFromServer();
                m_server->close();
                releaseLock();
                QCoreApplication::quit();
                return;
            }
            socket->write("ok\n");
            socket->flush();
            socket->disconnectFromServer();
            // No more launches here: the next one cold-starts or finds the
            // next spare. Then the next spare may start.
            m_server->close();
            releaseLock();
            emit woke(args);
        });
    }
}
