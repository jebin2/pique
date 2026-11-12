// One peeked page. Extra clicks and key chords are not given their own action.
import QtQuick
import QtWebEngine

Item {
    id: layer

    property WebEngineProfile profile
    property alias view: web

    WebEngineView {
        id: web
        anchors.fill: parent
        profile: layer.profile
        backgroundColor: "#0e0e10"
        settings.javascriptCanOpenWindows: false
        settings.playbackRequiresUserGesture: false
        // Links that ask for a new tab or window (target="_blank", middle
        // click) open right here. A peek has one page, and an ignored
        // request would make the click do nothing.
        onNewWindowRequested: (request) => request.openIn(web)
    }

    // A gentle settle, like Arc's peek layer arriving.
    function popIn() { enter.start() }

    ParallelAnimation {
        id: enter
        NumberAnimation { target: layer; property: "opacity"; from: 0; to: 1; duration: 130; easing.type: Easing.OutCubic }
        NumberAnimation { target: layer; property: "scale"; from: 0.98; to: 1; duration: 130; easing.type: Easing.OutCubic }
    }
}
