#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "standby.h"
#include "hyprland.h"
#include "theme.h"
#include "translator.h"

class PiqueTests : public QObject {
    Q_OBJECT

private slots:
    void normalizeOnlyPassesWholeAddresses() {
        Backend backend;
        QCOMPARE(backend.normalize("https://x.com/a?b=c"), QStringLiteral("https://x.com/a?b=c"));
        QCOMPARE(backend.normalize("see https://x.com for details"),
                 QStringLiteral("https://duckduckgo.com/?q=see%20https%3A%2F%2Fx.com%20for%20details"));
        QCOMPARE(backend.normalize("about:blank"), QStringLiteral("about:blank"));
    }

    void toolsFollowThePage() {
        auto client = [](const char *json) { return QJsonDocument::fromJson(json).object(); };
        const QJsonObject page = client(R"({"address":"0xp","at":[100,200],"size":[800,600],"pinned":false,"workspace":{"name":"1"}})");
        // Parked: top right of the page, 8 px up and in.
        QVERIFY(Hyprland::followStep(page, client(R"({"address":"0xt","at":[592,130],"size":[300,62],"pinned":false,"workspace":{"name":"1"}})")).isEmpty());
        // Out of place: moved there.
        QCOMPARE(Hyprland::followStep(page, client(R"({"address":"0xt","at":[0,0],"size":[300,62],"pinned":false,"workspace":{"name":"1"}})")),
                 QStringLiteral("hl.dsp.window.move({ window = \"address:0xt\", x = 592, y = 130, relative = false })"));
        // Page on another workspace: the icons go there first.
        QCOMPARE(Hyprland::followStep(page, client(R"({"address":"0xt","at":[592,130],"size":[300,62],"pinned":false,"workspace":{"name":"9"}})")),
                 QStringLiteral("hl.dsp.window.move({ workspace = \"1\", follow = false, window = \"address:0xt\" })"));
        // Page pinned: the icons are pinned too, before anything else.
        const QJsonObject pinned = client(R"({"address":"0xp","at":[100,200],"size":[800,600],"pinned":true,"workspace":{"name":"1"}})");
        QCOMPARE(Hyprland::followStep(pinned, client(R"({"address":"0xt","at":[0,0],"size":[300,62],"pinned":false,"workspace":{"name":"9"}})")),
                 QStringLiteral("hl.dsp.window.pin({ window = \"address:0xt\" })"));
    }

    void standbyParsesLaunches() {
        Launch launch = Standby::parse({QStringLiteral("--standby")});
        QVERIFY(launch.standby);
        QVERIFY(launch.url.isEmpty());
        launch = Standby::parse({QStringLiteral("--prefill=  some text  ")});
        QCOMPARE(launch.prefill, QStringLiteral("some text"));
        QVERIFY(!launch.standby);
        launch = Standby::parse({QStringLiteral("one"), QStringLiteral("https://two.example")});
        QCOMPARE(launch.url, QStringLiteral("https://two.example"));
        QVERIFY(Standby::parse({QStringLiteral("--stop-standby")}).stopStandby);
        // Unknown flags are not addresses.
        QVERIFY(Standby::parse({QStringLiteral("--whatever")}).url.isEmpty());
    }

    void peekArgsPickLinkThenSelection() {
        QCOMPARE(Backend::peekArgs("https://a.com/x", "ignored"), QStringList{"https://a.com/x"});
        QCOMPARE(Backend::peekArgs("", "  https://b.com  "), QStringList{"https://b.com"});
        QCOMPARE(Backend::peekArgs("", "how far is the moon"), QStringList{"--prefill=how far is the moon"});
        QCOMPARE(Backend::peekArgs("javascript:alert(1)", ""), QStringList{});
        QCOMPARE(Backend::peekArgs("", "   "), QStringList{});
    }

    void normalizeAddsHttpsToBareHosts() {
        Backend backend;
        QCOMPARE(backend.normalize("example.com"), QStringLiteral("https://example.com"));
        QCOMPARE(backend.normalize("example.com/x?y=1"), QStringLiteral("https://example.com/x?y=1"));
        QCOMPARE(backend.normalize("  example.com  "), QStringLiteral("https://example.com"));
        QCOMPARE(backend.normalize("localhost:3000"), QStringLiteral("https://localhost:3000"));
        QCOMPARE(backend.normalize("192.168.1.4:8080/admin"), QStringLiteral("https://192.168.1.4:8080/admin"));
    }

    void normalizeLeavesSchemesAndAboutAlone() {
        Backend backend;
        QCOMPARE(backend.normalize("http://example.com"), QStringLiteral("http://example.com"));
        QCOMPARE(backend.normalize("https://example.com/a b"), QStringLiteral("https://example.com/a b"));
        QCOMPARE(backend.normalize("about:blank"), QStringLiteral("about:blank"));
        QCOMPARE(backend.normalize("file:///tmp/x.html"), QStringLiteral("file:///tmp/x.html"));
    }

    void normalizeSearchesForEverythingElse() {
        Backend backend;
        QCOMPARE(backend.normalize("hello world"), QStringLiteral("https://duckduckgo.com/?q=hello%20world"));
        QCOMPARE(backend.normalize(""), QStringLiteral(""));
        QCOMPARE(backend.normalize("   "), QStringLiteral(""));
    }

    void notesAndClearsMessages() {
        Backend backend;
        QVERIFY(backend.message().isEmpty());
        backend.note(QStringLiteral("boom"));
        QCOMPARE(backend.message(), QStringLiteral("boom"));
        backend.clearMessage();
        QVERIFY(backend.message().isEmpty());
    }

    void keepReportsAMissingLauncher() {
        QTemporaryDir dir;
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", dir.path().toUtf8());
        Backend backend;
        QSignalSpy kept(&backend, &Backend::kept);
        backend.keep(QStringLiteral("https://example.com"));
        qputenv("PATH", oldPath);
        QCOMPARE(kept.count(), 0);
        QVERIFY(!backend.message().isEmpty());
    }

    void keepHandsThePageToTheBrowser() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("bin")));
        const QString script = dir.filePath(QStringLiteral("bin/omarchy-launch-browser"));
        const QString marker = dir.filePath(QStringLiteral("kept.txt"));
        QVERIFY(write(script, "#!/bin/sh\necho \"$@\" > '" + marker.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", (dir.path() + QStringLiteral("/bin")).toUtf8());
        Backend backend;
        QSignalSpy kept(&backend, &Backend::kept);
        backend.keep(QStringLiteral("https://example.com/kept"));
        qputenv("PATH", oldPath);

        QCOMPARE(kept.count(), 1);
        QVERIFY(backend.message().isEmpty());
        QTRY_VERIFY(QFile::exists(marker));
        QFile result(marker);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QVERIFY(result.readAll().contains("https://example.com/kept"));
    }

    void copyUrlPutsTheAddressOnTheClipboard() {
        Backend backend;
        backend.copyUrl(QStringLiteral("https://example.com/copied"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("https://example.com/copied"));
        QCOMPARE(backend.message(), QStringLiteral("Copied."));

        backend.clearMessage();
        backend.copyUrl(QString());
        QVERIFY(!backend.message().isEmpty());
    }

    void hyprWindowMatchesThisProcess() {
        const QByteArray clients = R"([
            {"pid": 4, "address": "0xabc", "floating": true, "pinned": false, "title": "other"},
            {"pid": 9, "address": "0xdef", "floating": false, "pinned": true, "title": "pique"}
        ])";
        const Hyprland::Window window = Hyprland::windowForPid(clients, 9);
        QVERIFY(window.found);
        QCOMPARE(window.address, QStringLiteral("0xdef"));
        QVERIFY(!window.floating);
        QVERIFY(window.pinned);
        QVERIFY(!Hyprland::windowForPid(clients, 3).found);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static bool write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(contents);
        return true;
    }
};

QTEST_MAIN(PiqueTests)
#include "pique_tests.moc"
