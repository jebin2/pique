#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QtGlobal>

// Hyprland owns this peek's window: float, tile, pin, and parking the icons
// above the page. Addresses and clipboard stay in Backend.
class Hyprland : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool floating READ floating NOTIFY windowChanged)
    Q_PROPERTY(bool pinned READ pinned NOTIFY windowChanged)
    Q_PROPERTY(bool toolsReady READ toolsReady NOTIFY toolsReadyChanged)
    Q_PROPERTY(bool toolsSettled READ toolsSettled NOTIFY toolsSettledChanged)

public:
    // One Hyprland client that belongs to this process, ignoring the icon row.
    struct Window {
        bool found = false;
        QString address;
        bool floating = false;
        bool pinned = false;
    };

    explicit Hyprland(QObject *parent = nullptr);

    bool floating() const { return m_floating; }
    bool pinned() const { return m_pinned; }
    bool toolsReady() const { return m_toolsReady; }
    bool toolsSettled() const { return m_toolsSettled; }

    // Drops the peek into the tiled workspace, or floats it again.
    Q_INVOKABLE void tileIntoWorkspace();
    // One query for float, pin, and the icon overlay. It does not wait on
    // the UI thread. Once the icons are sitting still, ask less often.
    Q_INVOKABLE void poll();

    static Window windowForPid(const QByteArray &clientsJson, qint64 pid);
    // The next hyprctl dispatch that brings the icon row to the page: same
    // pin state, same workspace, then parked above the page's top right
    // corner. Empty once it is there. One step per poll.
    static QString followStep(const QJsonObject &page, const QJsonObject &tools);

signals:
    void windowChanged();
    void toolsReadyChanged();
    void toolsSettledChanged();
    void failed(const QString &message);

private:
    bool dispatch(const QString &lua);
    Window thisWindow();
    void applyClients(const QByteArray &clientsJson);
    void setToolsReady(bool ready);
    void setToolsSettled(bool settled);

    bool m_floating = true;
    bool m_pinned = false;
    bool m_toolsReady = false;
    bool m_toolsSettled = false;
    QProcess m_clients;
    QProcess m_move;
    // Keys belong to the page. Hyprland focuses the newest window, the icon
    // row, so the page is focused once when both windows have appeared.
    QProcess m_focus;
    bool m_pageFocused = false;
};
