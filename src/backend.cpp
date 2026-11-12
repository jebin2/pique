#include "backend.h"
#include "standby.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

void Backend::note(const QString &message) {
    m_message = message;
    // Always signal, so a second copy of the same line restarts the notice.
    emit messageChanged();
}

void Backend::clearMessage() {
    if (m_message.isEmpty())
        return;
    m_message.clear();
    emit messageChanged();
}

QString Backend::normalize(const QString &input) const {
    const QString text = input.trimmed();
    if (text.isEmpty())
        return {};
    // An address starts with its scheme. "see https://x.com for details" is
    // a search, not an address.
    static const QRegularExpression scheme(QStringLiteral(R"(^([A-Za-z][A-Za-z0-9+.-]*://|about:))"));
    if (scheme.match(text).hasMatch())
        return text;
    // A bare host with an optional port and path: localhost, an IP address,
    // or a dotted name ending in a real suffix, with no spaces in it.
    static const QRegularExpression host(QStringLiteral(
        R"(^(localhost|([0-9]{1,3}\.){3}[0-9]{1,3}|([A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?\.)+[A-Za-z]{2,63})(:[0-9]{1,5})?(/[^\s]*)?$)"));
    if (host.match(text).hasMatch())
        return QStringLiteral("https://") + text;
    return QStringLiteral("https://duckduckgo.com/?q=") + QString::fromUtf8(QUrl::toPercentEncoding(text));
}

void Backend::keep(const QString &url) {
    const QString launcher = QStandardPaths::findExecutable(QStringLiteral("omarchy-launch-browser"));
    if (launcher.isEmpty()) {
        note(tr("omarchy-launch-browser was not found — set a default browser with 'omarchy default browser <browser>'."));
        return;
    }
    if (!QProcess::startDetached(launcher, {url})) {
        note(tr("%1 could not be started.").arg(QStringLiteral("omarchy-launch-browser")));
        return;
    }
    emit kept();
}

QStringList Backend::peekArgs(const QString &link, const QString &text) {
    if (link.startsWith(QStringLiteral("http://")) || link.startsWith(QStringLiteral("https://")))
        return {link};
    const QString selected = text.trimmed().left(2000);
    if (selected.isEmpty())
        return {};
    static const QRegularExpression address(QStringLiteral(R"(^(https?://|www\.)\S+$)"));
    if (address.match(selected).hasMatch())
        return {selected};
    return {QStringLiteral("--prefill=") + selected};
}

QPointF Backend::askedPoint() const {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    QFile file(runtime + QStringLiteral("/pique-ask"));
    if (runtime.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {-1, -1};
    const QJsonObject ask = QJsonDocument::fromJson(file.read(4096)).object();
    const double at = ask.value(QStringLiteral("at")).toDouble();
    if (QDateTime::currentMSecsSinceEpoch() / 1000.0 - at > 3)
        return {-1, -1};  // left over from an earlier question
    return {ask.value(QStringLiteral("x")).toDouble(-1), ask.value(QStringLiteral("y")).toDouble(-1)};
}

void Backend::answerPeek(const QString &link, const QString &text) {
    // Same marker as the browser extension's host: pique-peek waits for it.
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) {
        QFile answered(runtime + QStringLiteral("/pique-answered"));
        if (answered.open(QIODevice::WriteOnly | QIODevice::Truncate))
            answered.close();
    }
    if (!Standby::launch(peekArgs(link, text)))
        note(tr("A new peek could not be started."));
}

bool Backend::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->nativeScanCode() == 196) {
            // One window gets the key; the press asks once.
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() && watched->isWindowType())
                emit peekAsked();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void Backend::copyUrl(const QString &url) {
    if (url.isEmpty()) {
        note(tr("Nothing to copy yet — open a page first."));
        return;
    }
    QGuiApplication::clipboard()->setText(url);
    note(tr("Copied."));
}

void Backend::wake(const QString &url, const QString &prefill) {
    m_initialUrl = url;
    m_initialSearch = prefill;
    emit startChanged();
    m_standby = false;
    emit standbyChanged();
    emit woke();
}
