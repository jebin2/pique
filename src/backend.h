#pragma once

#include <QObject>
#include <QPointF>

// Addresses, clipboard, and handing a page to the real browser. The window
// manager lives in Hyprland.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString initialUrl READ initialUrl NOTIFY startChanged)
    // Text for the search bar when pique opens empty: highlighted text that
    // is not an address. Shown selected, so Enter searches it and typing
    // replaces it.
    Q_PROPERTY(QString initialSearch READ initialSearch NOTIFY startChanged)
    // A spare waiting hidden for the next launch (Standby).
    Q_PROPERTY(bool standby READ standby NOTIFY standbyChanged)

public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}

    QString message() const { return m_message; }
    QString initialUrl() const { return m_initialUrl; }
    QString initialSearch() const { return m_initialSearch; }
    bool standby() const { return m_standby; }

    Q_INVOKABLE void note(const QString &message);
    Q_INVOKABLE void clearMessage();
    // Turns typed text into a URL: schemes pass through, bare hosts get
    // https://, and everything else becomes a DuckDuckGo search.
    Q_INVOKABLE QString normalize(const QString &input) const;
    // Hands the page to Omarchy's default browser; reports a problem in the
    // window instead of failing quietly.
    Q_INVOKABLE void keep(const QString &url);
    // Copies the page address. A peek has no address bar, so this is how the
    // URL leaves the window.
    Q_INVOKABLE void copyUrl(const QString &url);

    // pique-peek asked this window (Super+Shift+L or click over a peek):
    // open what was under the pointer or selected, in a new peek, and tell
    // pique-peek it was answered.
    Q_INVOKABLE void answerPeek(const QString &link, const QString &text);
    // The new peek's arguments: a link, a selected address, selected text
    // for the search bar, or nothing.
    static QStringList peekArgs(const QString &link, const QString &text);
    // Where pique-peek saw the pointer in this window when it asked, or
    // (-1, -1). Read from $XDG_RUNTIME_DIR/pique-ask, written just before.
    Q_INVOKABLE QPointF askedPoint() const;

    void setInitialUrl(const QString &url) { m_initialUrl = url; }
    void setInitialSearch(const QString &text) { m_initialSearch = text; }
    void setStandby(bool standby) { m_standby = standby; }
    // A launch reached the spare: show the window with these.
    void wake(const QString &url, const QString &prefill);

    // pique-peek asks a peek what is under the pointer by sending it key code
    // 196 (pique-peek, browser/peek.js). Caught by scan code before the page
    // sees it: its key name depends on the keyboard layout.
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void peekAsked();
    void startChanged();
    void standbyChanged();
    void woke();
    void messageChanged();
    void kept();

private:
    QString m_message;
    QString m_initialUrl;
    QString m_initialSearch;
    bool m_standby = false;
};
