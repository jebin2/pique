#include "hyprland.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

Hyprland::Hyprland(QObject *parent) : QObject(parent) {
    connect(&m_clients, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (code != 0 || status != QProcess::NormalExit)
            return;
        applyClients(m_clients.readAllStandardOutput());
    });
}

Hyprland::Window Hyprland::windowForPid(const QByteArray &clientsJson, qint64 pid) {
    Window window;
    const QJsonArray clients = QJsonDocument::fromJson(clientsJson).array();
    for (const QJsonValue &value : clients) {
        const QJsonObject client = value.toObject();
        if (client.value(QStringLiteral("pid")).toInteger() != pid)
            continue;
        // The icon overlay is another window of this process. Tile and pin
        // belong to the page.
        if (client.value(QStringLiteral("title")).toString() == QStringLiteral("pique-tools"))
            continue;
        window.found = true;
        window.address = client.value(QStringLiteral("address")).toString();
        window.floating = client.value(QStringLiteral("floating")).toBool();
        window.pinned = client.value(QStringLiteral("pinned")).toBool();
        return window;
    }
    return window;
}

Hyprland::Window Hyprland::thisWindow() {
    QProcess hyprctl;
    hyprctl.start(QStringLiteral("hyprctl"), {QStringLiteral("clients"), QStringLiteral("-j")});
    if (!hyprctl.waitForFinished(2000))
        return {};
    const Window window = windowForPid(hyprctl.readAllStandardOutput(), QCoreApplication::applicationPid());
    if (window.found && (m_floating != window.floating || m_pinned != window.pinned)) {
        m_floating = window.floating;
        m_pinned = window.pinned;
        emit windowChanged();
    }
    return window;
}

bool Hyprland::dispatch(const QString &lua) {
    QProcess hyprctl;
    hyprctl.start(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), lua});
    return hyprctl.waitForFinished(2000) && hyprctl.exitCode() == 0;
}

void Hyprland::setToolsReady(bool ready) {
    if (m_toolsReady == ready)
        return;
    m_toolsReady = ready;
    emit toolsReadyChanged();
}

void Hyprland::setToolsSettled(bool settled) {
    if (m_toolsSettled == settled)
        return;
    m_toolsSettled = settled;
    emit toolsSettledChanged();
}

void Hyprland::poll() {
    if (m_clients.state() != QProcess::NotRunning)
        return;
    m_clients.start(QStringLiteral("hyprctl"), {QStringLiteral("clients"), QStringLiteral("-j")});
}

void Hyprland::applyClients(const QByteArray &clientsJson) {
    const QJsonArray clients = QJsonDocument::fromJson(clientsJson).array();
    const qint64 pid = QCoreApplication::applicationPid();
    const Window window = windowForPid(clientsJson, pid);
    if (window.found && (m_floating != window.floating || m_pinned != window.pinned)) {
        m_floating = window.floating;
        m_pinned = window.pinned;
        emit windowChanged();
    }

    QJsonObject page;
    QJsonObject tools;
    bool havePage = false;
    bool haveTools = false;
    for (const QJsonValue &value : clients) {
        const QJsonObject client = value.toObject();
        if (client.value(QStringLiteral("pid")).toInteger() != pid)
            continue;
        if (client.value(QStringLiteral("title")).toString() == QStringLiteral("pique-tools")) {
            tools = client;
            haveTools = true;
        } else {
            page = client;
            havePage = true;
        }
    }
    if (!havePage || !haveTools) {
        // The icon row is new again when it comes back (after fullscreen),
        // and Hyprland would give it the keys.
        if (havePage)
            m_pageFocused = false;
        setToolsSettled(false);
        return;
    }
    // Keys belong to the page. pique-hypr's rule keeps Hyprland from
    // focusing the icon row; without it (focusHistoryID 0 = focused now),
    // hand the focus over once. A focus dispatch also moves the pointer to
    // the window, so the pointer is put back where it was.
    const QString pageAddress = page.value(QStringLiteral("address")).toString();
    if (!m_pageFocused && !pageAddress.isEmpty() && m_focus.state() == QProcess::NotRunning) {
        m_pageFocused = true;
        if (tools.value(QStringLiteral("focusHistoryID")).toInt(-1) == 0) {
            QProcess cursor;
            cursor.start(QStringLiteral("hyprctl"), {QStringLiteral("cursorpos"), QStringLiteral("-j")});
            const QJsonObject at = cursor.waitForFinished(1000) ? QJsonDocument::fromJson(cursor.readAllStandardOutput()).object() : QJsonObject();
            QString batch = QStringLiteral("dispatch hl.dsp.focus({ window = \"address:%1\" })").arg(pageAddress);
            if (at.contains(QStringLiteral("x")))
                batch += QStringLiteral(" ; dispatch hl.dsp.cursor.move({ x = %1, y = %2 })")
                             .arg(at.value(QStringLiteral("x")).toInt())
                             .arg(at.value(QStringLiteral("y")).toInt());
            m_focus.start(QStringLiteral("hyprctl"), {QStringLiteral("--batch"), batch});
        }
    }
    const QString step = followStep(page, tools);
    if (step.isEmpty()) {
        setToolsReady(true);
        setToolsSettled(true);
        return;
    }
    setToolsSettled(false);
    if (m_move.state() != QProcess::NotRunning)
        return;
    m_move.start(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), step});
}

QString Hyprland::followStep(const QJsonObject &page, const QJsonObject &tools) {
    const QString address = tools.value(QStringLiteral("address")).toString();
    if (address.isEmpty())
        return {};
    const QString target = QStringLiteral("window = \"address:%1\"").arg(address);

    // A pinned page shows on every workspace; so must its icons.
    const bool pagePinned = page.value(QStringLiteral("pinned")).toBool();
    if (pagePinned != tools.value(QStringLiteral("pinned")).toBool())
        return QStringLiteral("hl.dsp.window.pin({ %1 })").arg(target);

    // The page was sent to another workspace: the icons go with it, without
    // switching the view there.
    const QString pageWorkspace = page.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("name")).toString();
    const QString toolsWorkspace = tools.value(QStringLiteral("workspace")).toObject().value(QStringLiteral("name")).toString();
    if (!pagePinned && !pageWorkspace.isEmpty() && pageWorkspace != toolsWorkspace)
        return QStringLiteral("hl.dsp.window.move({ workspace = \"%1\", follow = false, %2 })").arg(pageWorkspace, target);

    const QJsonArray pageAt = page.value(QStringLiteral("at")).toArray();
    const QJsonArray pageSize = page.value(QStringLiteral("size")).toArray();
    const QJsonArray toolsAt = tools.value(QStringLiteral("at")).toArray();
    const QJsonArray toolsSize = tools.value(QStringLiteral("size")).toArray();
    if (pageAt.size() < 2 || pageSize.size() < 2 || toolsAt.size() < 2 || toolsSize.size() < 2)
        return {};
    const int x = pageAt.at(0).toInt() + pageSize.at(0).toInt() - toolsSize.at(0).toInt() - 8;
    const int y = qMax(0, pageAt.at(1).toInt() - toolsSize.at(1).toInt() - 8);
    if (qAbs(toolsAt.at(0).toInt() - x) <= 2 && qAbs(toolsAt.at(1).toInt() - y) <= 2)
        return {};
    return QStringLiteral("hl.dsp.window.move({ %1, x = %2, y = %3, relative = false })").arg(target).arg(x).arg(y);
}

void Hyprland::tileIntoWorkspace() {
    const Window window = thisWindow();
    if (!window.found || window.address.isEmpty()) {
        emit failed(tr("This window is not managed by Hyprland."));
        return;
    }
    const QString action = window.floating ? QStringLiteral("off") : QStringLiteral("on");
    const QString lua = QStringLiteral("hl.dsp.window.float({ window = \"address:%1\", action = \"%2\" })").arg(window.address, action);
    if (!dispatch(lua))
        emit failed(window.floating ? tr("The window could not join the workspace.") : tr("The window could not float."));
    thisWindow();
}
