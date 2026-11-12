// pique — the window: one page and the overlay buttons. Layout only; the
// logic lives in the C++ backend.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebEngine

ApplicationWindow {
    id: win
    width: 780
    height: 600
    minimumWidth: 420
    minimumHeight: 320
    // A spare (pique --standby) waits hidden until a launch wakes it.
    visible: !backend.standby
    title: currentView && currentView.title ? currentView.title + " — pique" : "pique"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // Pages stack up: a base page, plus one layer per peeked link.
    property var layerStack: []
    property WebEngineView currentView: null
    property bool readerOn: false
    property bool shuttingDown: false

    function shutdown() {
        if (shuttingDown)
            return
        shuttingDown = true
        dock.close()
        close()
    }

    function openBase(url) {
        const target = url ? backend.normalize(url) : ""
        if (!target)
            return
        ensureProfile()
        const layer = peekLayer.createObject(content, {})
        layer.width = Qt.binding(() => content.width)
        layer.height = Qt.binding(() => content.height)
        // A new array, not push(): QML only re-reads a var property when it is
        // assigned, and the search bar hides on layerStack.length.
        layerStack = layerStack.concat([layer])
        currentView = layer.view
        layer.popIn()
        layer.view.url = target
        // The search bar is gone; keys go to the page now.
        layer.view.forceActiveFocus()
    }

    function currentUrl() {
        return currentView ? currentView.url.toString() : ""
    }

    function copyCurrent() {
        backend.copyUrl(currentUrl())
    }

    // Keep it: the page moves to the real browser and the peek closes.
    function keepCurrent() {
        const url = currentUrl()
        if (!url) {
            backend.note(qsTr("Nothing to keep yet — open a page first."))
            return
        }
        backend.keep(url)
    }

    // A readable column over the page. The script returns whether that
    // column is showing, so the same button turns it off again.
    function toggleReader() {
        if (!currentView)
            return
        currentView.runJavaScript("(function(){var id='pique-reader';var old=document.getElementById(id);if(old){old.remove();return false;}var source=document.querySelector('article')||document.querySelector('main')||document.body;var overlay=document.createElement('div');overlay.id=id;overlay.setAttribute('style','position:fixed;inset:0;z-index:2147483646;overflow:auto;background:#16161a;color:#ececf1;');var column=document.createElement('div');column.setAttribute('style','max-width:40rem;margin:3.5rem auto 2.5rem;padding:0 1.25rem;font:1.125rem/1.65 Georgia,serif;');column.innerHTML=source.innerHTML.replace(/<script[\\s\\S]*?<\\/script>/gi,'');overlay.appendChild(column);document.documentElement.appendChild(overlay);return true;})()", function(on) { readerOn = on === true })
    }

    function syncReader() {
        if (!currentView) {
            readerOn = false
            return
        }
        currentView.runJavaScript("!!document.getElementById('pique-reader')", function(on) { readerOn = on === true })
    }

    Connections {
        target: backend
        function onKept() { win.shutdown() }
        function onMessageChanged() { if (backend.message) noticeTimer.restart() }
    }

    Connections {
        target: win.currentView
        function onUrlChanged() { win.syncReader() }
        function onRenderProcessTerminated(terminationStatus, exitCode) {
            backend.note(qsTr("The page renderer stopped."))
        }
        function onCertificateError(error) {
            error.rejectCertificate()
            backend.note(qsTr("The site's security certificate was rejected."))
        }
        function onPermissionRequested(permission) {
            permission.deny()
            backend.note(qsTr("A permission request from the page was denied."))
        }
        // A video's fullscreen button fills the screen; leaving it, from the
        // page or from Hyprland, brings the peek back.
        function onFullScreenRequested(request) {
            request.accept()
            if (request.toggleOn)
                win.showFullScreen()
            else
                win.showNormal()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Component {
                    id: peekLayer
                    PeekLayer { profile: win.piqueProfile }
                }

                // The empty start state: nothing peeked yet.
                Column {
                    id: emptyState
                    z: 10
                    anchors.centerIn: parent
                    spacing: 16
                    width: Math.min(parent.width - 64, 460)
                    visible: layerStack.length === 0
                    onVisibleChanged: {
                        if (visible)
                            urlInput.forceActiveFocus()
                    }

                    Label {
                        text: "pique"
                        font.pixelSize: 28
                        font.bold: true
                        color: "#e8e8ea"
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Rectangle {
                        width: parent.width
                        height: 44
                        radius: 22
                        color: "#16161a"
                        border.color: urlInput.activeFocus ? theme.accent : "#38ffffff"
                        border.width: urlInput.activeFocus ? 2 : 1

                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        Text {
                            id: urlPlaceholder
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("Search DuckDuckGo or enter address…")
                            color: "#5d5d66"
                            font.pixelSize: 14
                            visible: urlInput.text.length === 0
                        }

                        TextInput {
                            id: urlInput
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            verticalAlignment: TextInput.AlignVCenter
                            color: "#ececf1"
                            font.pixelSize: 14
                            clip: true
                            selectByMouse: true
                            selectionColor: theme.accent
                            selectedTextColor: "#0e0e10"
                            focus: true
                            onAccepted: {
                                const trimmed = text.trim()
                                if (trimmed.length > 0)
                                    win.openBase(trimmed)
                            }
                            Keys.onEscapePressed: win.shutdown()
                        }
                    }

                    Label {
                        text: qsTr("Summoned with Super+Shift+L or Super+Shift+click.")
                        color: "#5d5d66"
                        font.pixelSize: 12
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                Item {
                    id: spinner
                    anchors.centerIn: parent
                    z: 5
                    width: 40
                    height: 40
                    visible: currentView && currentView.loading && currentView.url.toString() !== ""

                    RotationAnimation on rotation {
                        from: 0
                        to: 360
                        duration: 800
                        loops: Animation.Infinite
                        running: spinner.visible
                    }

                    Repeater {
                        model: 8
                        Rectangle {
                            required property int index
                            width: 6
                            height: 6
                            radius: 3
                            color: theme.accent
                            opacity: 0.18 + 0.82 * index / 7
                            x: 17 + Math.cos(index / 8 * Math.PI * 2) * 14
                            y: 17 + Math.sin(index / 8 * Math.PI * 2) * 14
                        }
                    }
                }

                Rectangle {
                    z: 8
                    visible: backend.message !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    width: Math.min(parent.width - 32, noticeText.implicitWidth + 28)
                    height: 36
                    radius: height / 2
                    color: "#e616161a"
                    border.color: "#59ffffff"
                    border.width: 1

                    Label {
                        id: noticeText
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: backend.message
                        textFormat: Text.PlainText
                        color: "#f2f2f4"
                        font.pixelSize: 13
                    }
                }
            }
        }

    }

    // A small overlay above the window, not a bar inside it. The page keeps
    // the whole window. Wayland clips anything drawn past the edge, so these
    // circles are their own frameless window and Hyprland parks it above.
    Window {
        id: dock
        property string hint: ""
        property Item hintButton: null
        title: "pique-tools"
        // A normal frameless window so a click reaches the buttons. A tool
        // window that refuses focus never gets the press.
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        onClosing: function(close) { win.shutdown() }
        color: "transparent"
        // Fixed size, so a tooltip never resizes or shifts the circles.
        // Extra room on the left lets a label sit above the icon it names.
        width: 300
        height: 62
        // Hidden while a video fills the screen.
        visible: !backend.standby && win.visibility !== Window.FullScreen
        opacity: hyprland.toolsReady ? 1 : 0

        Item {
            anchors.fill: parent

            Rectangle {
                id: tip
                visible: dock.hint !== "" && dock.hintButton !== null
                width: hintText.implicitWidth + 14
                height: hintText.implicitHeight + 6
                radius: height / 2
                color: "#f216161a"
                border.color: "#59ffffff"
                border.width: 1
                y: buttonRow.y - height - 4
                x: {
                    if (!dock.hintButton)
                        return 0
                    const center = buttonRow.x + dock.hintButton.x + dock.hintButton.width / 2
                    return Math.max(0, Math.min(center - width / 2, parent.width - width))
                }

                Label {
                    id: hintText
                    anchors.centerIn: parent
                    text: dock.hint
                    textFormat: Text.PlainText
                    color: "#f2f2f4"
                    font.pixelSize: 12
                }
            }

            // Separate circles: reader mode, copy the URL, keep it in the
            // browser, tile into the workspace, close. (Pinning is Hyprland's
            // own; the icons follow a pinned page.)
            component RoundTool: ToolButton {
                id: tool
                property color mark: "#e8e8ea"
                property string hint: ""
                width: 36
                height: 36
                padding: 0
                flat: true
                display: AbstractButton.IconOnly
                icon.width: 16
                icon.height: 16
                icon.color: mark
                background: Rectangle {
                    radius: width / 2
                    color: tool.hovered ? "#f21c1c22" : "#e616161a"
                    border.color: tool.hovered ? "#73ffffff" : "#40ffffff"
                    border.width: 1
                }
                onHoveredChanged: {
                    if (hovered) {
                        dock.hint = hint
                        dock.hintButton = tool
                    } else if (dock.hintButton === tool) {
                        dock.hint = ""
                        dock.hintButton = null
                    }
                }
            }

            Row {
                id: buttonRow
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8

                RoundTool {
                    icon.source: "qrc:/icons/reader.svg"
                    hint: qsTr("Reader mode")
                    mark: win.readerOn ? theme.accent : "#e8e8ea"
                    onClicked: win.toggleReader()
                }
                RoundTool {
                    icon.source: "qrc:/icons/copy.svg"
                    hint: qsTr("Copy the URL")
                    onClicked: win.copyCurrent()
                }
                RoundTool {
                    icon.source: "qrc:/icons/keep.svg"
                    hint: qsTr("Open in your browser")
                    onClicked: win.keepCurrent()
                }
                RoundTool {
                    objectName: "tileTool"
                    icon.source: "qrc:/icons/tile.svg"
                    hint: hyprland.floating ? qsTr("Tile into the workspace") : qsTr("Float")
                    mark: hyprland.floating ? "#e8e8ea" : theme.accent
                    onClicked: hyprland.tileIntoWorkspace()
                }
                RoundTool {
                    icon.source: "qrc:/icons/close.svg"
                    hint: qsTr("Close")
                    onClicked: win.shutdown()
                }
            }
        }
    }

    // Off the record on purpose: a peek leaves nothing behind — no history
    // and no cookies on disk; closing the window ends the session. Qt warns
    // that creating WebEngineProfile in QML is deprecated, but its
    // replacement cannot build an off-the-record profile yet.
    // Created by ensureProfile(): a waiting spare has no web engine yet.
    property WebEngineProfile piqueProfile: null
    Component {
        id: profileComponent
    WebEngineProfile {
        offTheRecord: true
        // Downloads are refused. A peek is for reading.
        onDownloadRequested: (download) => {
            download.cancel()
            backend.note(qsTr("Downloads are not saved by a peek."))
        }
    }
    }

    Timer {
        id: noticeTimer
        interval: 2200
        onTriggered: backend.clearMessage()
    }

    onVisibilityChanged: function(visibility) {
        if (visibility !== Window.FullScreen && currentView && currentView.isFullScreen)
            currentView.fullScreenCancelled()
    }

    // Esc leaves a video's fullscreen. Qt WebEngine leaves that key to the
    // app; the page itself never gets to keep the screen.
    Shortcut {
        sequences: ["Esc"]
        enabled: win.visibility === Window.FullScreen
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.currentView)
                win.currentView.fullScreenCancelled()
            win.showNormal()
        }
    }

    // pique-peek's question (Backend.peekAsked): Super+Shift+L or
    // Super+Shift+click over this peek. The answer is read from the page now:
    // the link under the pointer, else the selected text, else nothing.
    Connections {
        target: backend
        function onPeekAsked() { win.answerPeek() }
    }

    function answerPeek() {
        if (!currentView) {
            backend.answerPeek("", urlInput.selectedText)
            return
        }
        // pique-peek says where the pointer is in this window. Neither the
        // page's :hover nor Qt Quick's hover tracking stays current here.
        const at = backend.askedPoint()
        const zoom = currentView.zoomFactor > 0 ? currentView.zoomFactor : 1
        const x = at.x >= 0 ? (at.x - content.x) / zoom : -1
        const y = at.y >= 0 ? (at.y - content.y) / zoom : -1
        currentView.runJavaScript("(function(x,y){var n=x>=0?document.elementFromPoint(x,y):null;var a=n&&n.closest?n.closest('a[href]'):null;var link=a&&/^https?:/.test(a.href)?a.href:'';var s=window.getSelection();return {link:link,text:link?'':(s?s.toString():'')};})(" + x + "," + y + ")",
            function(r) { backend.answerPeek(r && r.link ? r.link : "", r && r.text ? r.text : "") })
    }

    // The only key action once a page is open: close the peek.
    Shortcut {
        sequences: [StandardKey.Close]
        context: Qt.ApplicationShortcut
        onActivated: win.shutdown()
    }

    onClosing: function(close) {
        if (shuttingDown)
            return
        shuttingDown = true
        dock.close()
    }

    Component.onCompleted: {
        if (!backend.standby)
            begin()
    }

    Connections {
        target: backend
        function onWoke() { win.begin() }
    }

    function ensureProfile() {
        if (!piqueProfile)
            piqueProfile = profileComponent.createObject(win)
    }

    // A spare starts the web engine only once it is woken, and only after the
    // window has drawn its first frame, so the window still appears at once.
    function afterFirstFrame(fn) {
        const once = function() {
            win.frameSwapped.disconnect(once)
            Qt.callLater(fn)
        }
        win.frameSwapped.connect(once)
    }

    function begin() {
        const url = backend.initialUrl
        if (!url) {
            if (backend.initialSearch) {
                urlInput.text = backend.initialSearch
                urlInput.selectAll()
            }
            urlInput.forceActiveFocus()
        }
        if (piqueProfile)
            openBase(url)
        else
            afterFirstFrame(() => { ensureProfile(); openBase(url) })
    }

    Timer {
        interval: 50
        repeat: true
        running: !backend.standby
        onTriggered: {
            hyprland.poll()
            const next = !hyprland.toolsReady ? 50 : (hyprland.toolsSettled ? 1000 : 120)
            if (interval !== next) {
                interval = next
                restart()
            }
        }
    }
}
