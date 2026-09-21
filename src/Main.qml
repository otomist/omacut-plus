import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia
import "Format.js" as Format

ApplicationWindow {
    id: win
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 460
    visible: true
    title: backend.source.toString() === "" ? "omacut" : "omacut — " + fileName(backend.source)
    readonly property bool hasVideo: backend.source.toString() !== ""
    readonly property color accent: backend.themeAccent
    readonly property color accentForeground: backend.themeAccentForeground
    readonly property bool audioOutputReady: audioOutput !== null
    property var audioOutput: null
    property string noticeText: ""
    property bool helpVisible: false
    property bool quitConfirmVisible: false
    // Subtitle mode: the caption lane and its editor, on top of everything the
    // trimmer already does.
    property bool captionMode: false
    property bool styleVisible: false
    property int selectedCaption: -1
    readonly property bool hasCaptions: captionModel.count > 0
    // The caption on screen at the playhead — what the export would burn into
    // this frame. captionRevision is in the call only to re-run the binding
    // when the captions themselves change.
    readonly property string activeCaptionText: captionTextAt(trimBar.playheadSec, captionRevision)
    // Single-key shortcuts go quiet while something has the keyboard, or typing
    // "z" into a caption would zoom the timeline instead of writing a z.
    readonly property bool typing: captionField.activeFocus || styleVisible
    readonly property string statusText: noticeText !== "" ? noticeText : backend.status

    // What the last export wrote, so quitting only warns about unexported work.
    // A trim spanning the whole video is never dirty — that's just the source.
    property real exportedStartSec: -1
    property real exportedEndSec: -1
    property real pendingExportStartSec: 0
    property real pendingExportEndSec: 0
    // Bumped by every caption edit, so an export can mark exactly the captions
    // it wrote as saved.
    property int captionRevision: 0
    property int exportedCaptionRevision: 0
    property int pendingCaptionRevision: 0
    readonly property bool trimDirty: hasVideo && backend.duration > 0
        && (trimBar.startSec > 0 || trimBar.endSec < backend.duration || hasCaptions)
        && (trimBar.startSec !== exportedStartSec || trimBar.endSec !== exportedEndSec
            || captionRevision !== exportedCaptionRevision)

    Material.theme: Material.Dark
    Material.accent: win.accent
    color: "#0e0e10"

    function fileName(url) {
        var s = url.toString();
        return s === "" ? "" : decodeURIComponent(s.substring(s.lastIndexOf('/') + 1));
    }
    function showNotice(text) {
        noticeText = text;
        noticeTimer.restart();
    }
    function openVideo() {
        backend.openVideoDialog();
    }
    function exportVideo() {
        if (!win.hasVideo || backend.duration <= 0 || backend.busy)
            return;
        pendingExportStartSec = trimBar.startSec;
        pendingExportEndSec = trimBar.endSec;
        pendingCaptionRevision = captionRevision;
        backend.exportDialog(trimBar.startSec, trimBar.endSec);
    }
    function ensureAudioOutput() {
        if (audioOutput === null && win.hasVideo)
            audioOutput = audioOutputComponent.createObject(win);
    }
    function releaseAudioOutput() {
        if (audioOutput === null)
            return;
        var oldAudioOutput = audioOutput;
        audioOutput = null;
        oldAudioOutput.destroy();
    }
    function togglePlay() {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        ensureAudioOutput();
        if (player.priming)
            player.finishPriming();
        if (player.playbackState === MediaPlayer.PlayingState) {
            player.pause();
            return;
        }
        // The pause-at-end clamp rounds to whole milliseconds and the player
        // snaps seeks to frames, so a finished clip can rest a fraction of a
        // millisecond before endSec. Treat anything within 10 ms of the end
        // as "at the end" or play would instantly re-pause instead of
        // restarting from the trim start.
        var pos = player.position / 1000;
        if (pos < trimBar.startSec || pos >= trimBar.endSec - 0.01)
            player.position = Math.round(trimBar.startSec * 1000);
        player.play();
    }
    function movePlayheadTo(seconds) {
        if (player.priming)
            player.finishPriming();
        trimBar.playheadSec = seconds;
        player.position = Math.round(seconds * 1000);
    }
    function seekBy(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        // The playhead lives inside the trim, same as scrubbing and preview.
        movePlayheadTo(Math.max(trimBar.startSec, Math.min(trimBar.playheadSec + seconds, trimBar.endSec)));
    }
    // Both edges park the playhead on themselves, so you see the frame you just
    // trimmed to — the same thing dragging a handle does. While zoomed, the
    // edges stop at the zoom window instead of the video bounds.
    function moveTrimStartTo(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        var minGap = Math.min(0.1, backend.duration);
        trimBar.startSec = Math.max(trimBar.windowStart, Math.min(seconds, trimBar.endSec - minGap));
        movePlayheadTo(trimBar.startSec);
    }
    function moveTrimEndTo(seconds) {
        if (!win.hasVideo || backend.duration <= 0)
            return;
        var minGap = Math.min(0.1, backend.duration);
        trimBar.endSec = Math.min(trimBar.windowEnd, Math.max(seconds, trimBar.startSec + minGap));
        movePlayheadTo(trimBar.endSec);
    }
    // Captions, in playback order, timed against the source video.
    ListModel {
        id: captionModel
    }

    // The one look they all share. The style panel edits this in place and
    // syncCaptions() ships it to the backend.
    QtObject {
        id: captionStyle
        property string fontFamily: win.defaultCaptionFont()
        property int fontSize: 72
        property bool bold: true
        property string textColor: "#ffffff"
        property string outlineColor: "#000000"
        property int outlineWidth: 5
        property bool box: false
        property string boxColor: "#000000"
        property int boxOpacity: 70
        property int marginV: 80
    }

    // A heavy, wide sans reads best over video; fall through what's commonly
    // installed before settling for whatever the system hands us.
    function defaultCaptionFont() {
        var preferred = ["Inter", "Poppins", "Montserrat", "Archivo", "Adwaita Sans",
                         "DejaVu Sans", "Noto Sans", "Liberation Sans"];
        var families = Qt.fontFamilies();
        for (var i = 0; i < preferred.length; ++i) {
            if (families.indexOf(preferred[i]) >= 0)
                return preferred[i];
        }
        return Qt.application.font.family;
    }

    // Caption sizes are in the video's own pixels, so the defaults are a share
    // of its height: the same caption looks the same on a 4K and a 720p source.
    function resetCaptionStyle() {
        var h = backend.videoHeight > 0 ? backend.videoHeight : 1080;
        captionStyle.fontFamily = defaultCaptionFont();
        captionStyle.fontSize = Math.max(12, Math.round(h * 0.075));
        captionStyle.bold = true;
        captionStyle.textColor = "#ffffff";
        captionStyle.outlineColor = "#000000";
        captionStyle.outlineWidth = Math.max(2, Math.round(h * 0.0054));
        captionStyle.box = false;
        captionStyle.boxColor = "#000000";
        captionStyle.boxOpacity = 70;
        captionStyle.marginV = Math.round(h * 0.08);
    }

    function captionStyleMap() {
        return {
            fontFamily: captionStyle.fontFamily,
            fontSize: captionStyle.fontSize,
            bold: captionStyle.bold,
            textColor: captionStyle.textColor,
            outlineColor: captionStyle.outlineColor,
            outlineWidth: captionStyle.outlineWidth,
            box: captionStyle.box,
            boxColor: captionStyle.boxColor,
            boxOpacity: captionStyle.boxOpacity,
            marginV: captionStyle.marginV
        };
    }

    // Every caption edit ends up here: the backend holds the list the export
    // burns in, and the revision marks the work as unexported.
    function syncCaptions() {
        var cues = [];
        for (var i = 0; i < captionModel.count; ++i) {
            var cue = captionModel.get(i);
            cues.push({ start: cue.startSec, end: cue.endSec, text: cue.text });
        }
        backend.setCaptions(cues, captionStyleMap());
        ++captionRevision;
    }

    function captionTextAt(seconds, revision) {
        for (var i = 0; i < captionModel.count; ++i) {
            var cue = captionModel.get(i);
            if (seconds >= cue.startSec && seconds <= cue.endSec)
                return cue.text;
        }
        return "";
    }

    // New captions run two seconds from where they're dropped, trimmed to what
    // is left of the video.
    function addCaptionAt(seconds) {
        if (!hasVideo || backend.duration <= 0)
            return;
        captionMode = true;
        var start = Math.max(0, Math.min(seconds, backend.duration - 0.2));
        var end = Math.min(start + 2, backend.duration);
        // Keep the list in playback order, so the lane reads left to right.
        var at = captionModel.count;
        for (var i = 0; i < captionModel.count; ++i) {
            if (captionModel.get(i).startSec > start) {
                at = i;
                break;
            }
        }
        captionModel.insert(at, { startSec: start, endSec: end, text: "" });
        selectCaption(at);
        syncCaptions();
        captionField.forceActiveFocus();
    }
    function addCaption() {
        addCaptionAt(trimBar.playheadSec);
    }

    function deleteCaption() {
        if (selectedCaption < 0 || selectedCaption >= captionModel.count)
            return;
        captionModel.remove(selectedCaption);
        selectCaption(Math.min(selectedCaption, captionModel.count - 1));
        syncCaptions();
    }

    function selectCaption(index) {
        selectedCaption = index >= 0 && index < captionModel.count ? index : -1;
        captionField.text = selectedCaption >= 0 ? captionModel.get(selectedCaption).text : "";
    }

    function setCaptionText(text) {
        if (selectedCaption < 0)
            return;
        captionModel.setProperty(selectedCaption, "text", text);
        syncCaptions();
    }

    function retimeCaption(index, start, end) {
        if (index < 0 || index >= captionModel.count)
            return;
        var cue = captionModel.get(index);
        var startMoved = start !== cue.startSec;
        if (!startMoved && end === cue.endSec)
            return;  // a drag that hasn't moved far enough to change anything
        captionModel.setProperty(index, "startSec", start);
        captionModel.setProperty(index, "endSec", end);
        // Park the playhead on the edge that moved, the same way dragging a trim
        // handle does, so you see the frame the caption now starts or ends on.
        movePlayheadTo(startMoved ? start : end);
        syncCaptions();
    }

    // [ and ] pull the selected caption's edges to the playhead, the caption
    // counterpart of the Ctrl/Alt+Space trim chords.
    function setCaptionEdge(isStart) {
        if (selectedCaption < 0)
            return;
        var cue = captionModel.get(selectedCaption);
        var minLen = 0.2;
        if (isStart)
            retimeCaption(selectedCaption,
                          Math.max(0, Math.min(trimBar.playheadSec, cue.endSec - minLen)),
                          cue.endSec);
        else
            retimeCaption(selectedCaption, cue.startSec,
                          Math.min(backend.duration,
                                   Math.max(trimBar.playheadSec, cue.startSec + minLen)));
    }

    property bool quitting: false
    function requestQuit() {
        if (trimDirty) {
            if (player.playbackState === MediaPlayer.PlayingState)
                player.pause();
            quitConfirmVisible = true;
            return;
        }
        forceQuit();
    }
    // Closing the window is what reliably ends the app (quitOnLastWindowClosed);
    // Qt.quit() alone has proven ignorable in a live session, so it's only the
    // backstop. The quitting flag stops onClosing from re-asking on the way out.
    function forceQuit() {
        quitting = true;
        player.stop();
        releaseAudioOutput();
        win.close();
        Qt.quit();
    }

    Component.onCompleted: ensureAudioOutput()
    onHasVideoChanged: {
        if (hasVideo) {
            ensureAudioOutput();
        } else {
            player.stop();
            releaseAudioOutput();
        }
    }
    onClosing: (close) => {
        if (win.quitting)
            return;
        if (win.trimDirty) {
            close.accepted = false;
            if (player.playbackState === MediaPlayer.PlayingState)
                player.pause();
            win.quitConfirmVisible = true;
            return;
        }
        forceQuit();
    }

    // The playback and trim shortcuts go quiet while the quit confirmation is
    // up — a disabled Shortcut also stops swallowing its key, which lets the
    // dialog's own keyboard navigation receive the arrows, Space and Enter.
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: togglePlay()
    }

    Shortcut {
        sequence: "Ctrl+Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: moveTrimStartTo(trimBar.playheadSec)
    }

    Shortcut {
        sequence: "Alt+Space"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: moveTrimEndTo(trimBar.playheadSec)
    }

    Shortcut {
        sequence: "Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(1)
    }

    Shortcut {
        sequence: "Shift+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(-5)
    }

    Shortcut {
        sequence: "Shift+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(5)
    }

    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(-0.2)
    }

    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && !win.quitConfirmVisible && !win.typing
        onActivated: seekBy(0.2)
    }

    Shortcut {
        sequence: "Z"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !win.quitConfirmVisible && !win.typing
        onActivated: {
            trimBar.toggleZoom();
            backend.requestThumbs(trimBar.windowStart, trimBar.windowEnd);
        }
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !backend.busy
        onActivated: {
            win.quitConfirmVisible = false;
            exportVideo();
        }
    }

    // --- subtitle mode ---
    Shortcut {
        sequence: "C"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !win.quitConfirmVisible && !win.typing
        onActivated: win.captionMode = !win.captionMode
    }

    Shortcut {
        sequence: "T"
        context: Qt.ApplicationShortcut
        enabled: win.hasVideo && backend.duration > 0 && !win.quitConfirmVisible && !win.typing
        onActivated: win.addCaption()
    }

    Shortcut {
        sequence: "["
        context: Qt.ApplicationShortcut
        enabled: win.selectedCaption >= 0 && !win.quitConfirmVisible && !win.typing
        onActivated: win.setCaptionEdge(true)
    }

    Shortcut {
        sequence: "]"
        context: Qt.ApplicationShortcut
        enabled: win.selectedCaption >= 0 && !win.quitConfirmVisible && !win.typing
        onActivated: win.setCaptionEdge(false)
    }

    Shortcut {
        sequence: "Delete"
        context: Qt.ApplicationShortcut
        enabled: win.selectedCaption >= 0 && !win.quitConfirmVisible && !win.typing
        onActivated: win.deleteCaption()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        enabled: !win.quitConfirmVisible
        onActivated: openVideo()
    }

    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            if (!win.quitConfirmVisible)
                requestQuit();
        }
    }

    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            if (!win.quitConfirmVisible)
                win.helpVisible = !win.helpVisible;
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.quitConfirmVisible)
                win.quitConfirmVisible = false;
            else if (win.styleVisible)
                win.styleVisible = false;
            else if (captionField.activeFocus)
                captionField.focus = false;
            else if (win.helpVisible)
                win.helpVisible = false;
        }
    }

    MediaPlayer {
        id: player
        source: backend.source
        videoOutput: videoOut
        audioOutput: win.audioOutput

        // Render the opening frame on load instead of showing black. Playback
        // starts muted and stops as soon as VideoOutput receives a frame.
        property bool primed: false
        property bool priming: false

        function startPriming() {
            if (primed || priming || backend.source.toString() === "")
                return;
            win.ensureAudioOutput();
            primed = true;
            priming = true;
            position = 0;
            play();
            primeFallback.restart();
        }

        function finishPriming() {
            if (!priming)
                return;
            primeFallback.stop();
            pause();
            position = 0;
            priming = false;
        }

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                startPriming();
        }
        onPositionChanged: {
            if (priming && position > 0) {
                finishPriming();
                return;
            }
            // Stop at the trim end, like a clip preview.
            if (playbackState === MediaPlayer.PlayingState && position / 1000 >= trimBar.endSec) {
                pause();
                position = Math.round(trimBar.endSec * 1000);
            }
            if (!trimBar.interacting)
                trimBar.playheadSec = position / 1000;
        }
    }

    Component {
        id: audioOutputComponent
        AudioOutput {
            muted: player.priming
        }
    }

    Timer {
        id: primeFallback
        interval: 250
        repeat: false
        onTriggered: player.finishPriming()
    }

    Timer {
        id: noticeTimer
        interval: 5000
        repeat: false
        onTriggered: win.noticeText = ""
    }

    component DialogButton: Rectangle {
        id: dialogButton
        // Implicit, not explicit: a Row leaves the size alone and a Layout can
        // still place it (the caption editor puts these in a RowLayout).
        implicitWidth: dialogButtonLabel.implicitWidth + 28
        implicitHeight: 34
        radius: 8

        property string text: ""
        property bool primary: false
        signal clicked()

        color: primary ? win.accent : "#2c2c2f"
        border.color: activeFocus ? (primary ? win.accentForeground : win.accent) : "transparent"
        border.width: activeFocus ? 2 : 0

        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onSpacePressed: clicked()

        Label {
            id: dialogButtonLabel
            anchors.centerIn: parent
            text: dialogButton.text
            color: dialogButton.primary ? win.accentForeground : "white"
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: dialogButton.clicked()
        }
    }

    component IconButton: Rectangle {
        id: iconButton
        implicitWidth: 44
        implicitHeight: 44
        radius: 22

        property string iconName: "play"
        property color iconColor: "white"
        property color buttonColor: "#2c2c2f"
        property string tipText: ""
        signal clicked()

        color: buttonColor
        opacity: enabled ? 1 : 0.45

        HoverHandler { id: iconHover }
        ToolTip.visible: iconHover.hovered && tipText !== ""
        ToolTip.text: tipText

        Canvas {
            id: iconCanvas
            anchors.centerIn: parent
            width: 24
            height: 24

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = iconButton.iconColor;
                ctx.strokeStyle = iconButton.iconColor;
                ctx.lineWidth = 2.4;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                if (iconButton.iconName === "pause") {
                    ctx.fillRect(7, 5, 4, 14);
                    ctx.fillRect(14, 5, 4, 14);
                } else if (iconButton.iconName === "play") {
                    ctx.beginPath();
                    ctx.moveTo(8, 5);
                    ctx.lineTo(8, 19);
                    ctx.lineTo(19, 12);
                    ctx.closePath();
                    ctx.fill();
                } else if (iconButton.iconName === "text") {
                    // A capital T: the subtitle mode toggle.
                    ctx.beginPath();
                    ctx.moveTo(5, 6);
                    ctx.lineTo(19, 6);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(12, 6);
                    ctx.lineTo(12, 19);
                    ctx.stroke();
                } else if (iconButton.iconName === "trash") {
                    ctx.beginPath();
                    ctx.moveTo(5, 7);
                    ctx.lineTo(19, 7);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(10, 7);
                    ctx.lineTo(10, 4);
                    ctx.lineTo(14, 4);
                    ctx.lineTo(14, 7);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(7, 7);
                    ctx.lineTo(8, 20);
                    ctx.lineTo(16, 20);
                    ctx.lineTo(17, 7);
                    ctx.stroke();
                } else if (iconButton.iconName === "download") {
                    ctx.beginPath();
                    ctx.moveTo(12, 4);
                    ctx.lineTo(12, 14);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(7, 10);
                    ctx.lineTo(12, 15);
                    ctx.lineTo(17, 10);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.moveTo(6, 20);
                    ctx.lineTo(18, 20);
                    ctx.stroke();
                }
            }

            Connections {
                target: iconButton
                function onIconNameChanged() { iconCanvas.requestPaint(); }
                function onIconColorChanged() { iconCanvas.requestPaint(); }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: iconButton.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: iconButton.clicked()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: win.hasVideo ? 16 : 0
        spacing: 14

        // --- video preview ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: win.hasVideo ? 12 : 0
            color: "black"
            clip: true

            VideoOutput {
                id: videoOut
                anchors.fill: parent
            }
            Connections {
                target: videoOut.videoSink
                function onVideoFrameChanged(frame) {
                    player.finishPriming();
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: openVideo()
            }

            // What the export will burn in, drawn where it will end up.
            CaptionOverlay {
                anchors.fill: parent
                visible: win.hasVideo
                text: win.activeCaptionText
                videoRect: videoOut.contentRect
                videoPixelHeight: backend.videoHeight
                fontFamily: captionStyle.fontFamily
                fontSize: captionStyle.fontSize
                bold: captionStyle.bold
                textColor: captionStyle.textColor
                outlineColor: captionStyle.outlineColor
                outlineWidth: captionStyle.outlineWidth
                box: captionStyle.box
                boxColor: captionStyle.boxColor
                boxOpacity: captionStyle.boxOpacity
                marginV: captionStyle.marginV
            }

            Button {
                id: openVideoButton
                anchors.centerIn: parent
                visible: !win.hasVideo
                text: "Open a video"
                highlighted: true
                focusPolicy: Qt.NoFocus
                font.pixelSize: 18
                Material.foreground: win.accentForeground
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
                contentItem: Label {
                    text: openVideoButton.text
                    font: openVideoButton.font
                    color: win.accentForeground
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: openVideo()
            }
        }

        // --- timeline ---
        RowLayout {
            visible: win.hasVideo
            Layout.fillWidth: true
            spacing: 10

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: player.playbackState === MediaPlayer.PlayingState && !player.priming ? "pause" : "play"
                tipText: player.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
                enabled: backend.duration > 0
                onClicked: togglePlay()
            }

            TrimBar {
                id: trimBar
                objectName: "trimBar"
                Layout.fillWidth: true
                accent: win.accent
                durationSec: backend.duration
                thumbCount: backend.thumbCount
                thumbReadyCount: backend.thumbReadyCount
                thumbRevision: backend.thumbRevision
                onScrub: (seconds) => player.position = Math.round(seconds * 1000)
            }

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: "text"
                tipText: win.captionMode ? "Close the subtitle editor (C)" : "Add subtitles (C)"
                buttonColor: win.captionMode ? win.accent : "#2c2c2f"
                iconColor: win.captionMode ? win.accentForeground : "white"
                enabled: backend.duration > 0
                onClicked: win.captionMode = !win.captionMode
            }

            IconButton {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                iconName: "download"
                tipText: "Export"
                enabled: backend.duration > 0 && !backend.busy
                onClicked: exportVideo()
            }
        }

        // --- subtitles ---
        // Indented by a button's width on each side, so the lane lines up with
        // the filmstrip it is timed against.
        ColumnLayout {
            visible: win.hasVideo && win.captionMode && backend.duration > 0
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.preferredWidth: 44 }

                CaptionTrack {
                    id: captionTrack
                    objectName: "captionTrack"
                    Layout.fillWidth: true
                    captions: captionModel
                    durationSec: backend.duration
                    windowStart: trimBar.windowStart
                    windowEnd: trimBar.windowEnd
                    trackX: trimBar.trackX
                    trackW: trimBar.trackW
                    playheadSec: trimBar.playheadSec
                    selectedIndex: win.selectedCaption
                    accent: win.accent
                    accentForeground: win.accentForeground
                    onPicked: (index) => {
                        win.selectCaption(index);
                        if (index >= 0)
                            win.movePlayheadTo(captionModel.get(index).startSec);
                    }
                    onRetimed: (index, start, end) => win.retimeCaption(index, start, end)
                    onAddRequested: (seconds) => win.addCaptionAt(seconds)
                }

                Item { Layout.preferredWidth: 44 }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.preferredWidth: 44 }

                DialogButton {
                    text: "+ Caption"
                    primary: true
                    onClicked: win.addCaption()
                }

                TextField {
                    id: captionField
                    objectName: "captionField"
                    Layout.fillWidth: true
                    enabled: win.selectedCaption >= 0
                    placeholderText: win.selectedCaption >= 0
                        ? "Caption text"
                        : "Add a caption, or pick one on the lane"
                    font.pixelSize: 14
                    color: "white"
                    selectByMouse: true
                    // onTextEdited, not onTextChanged: selecting another caption
                    // refills the field, and that must not write it back.
                    onTextEdited: win.setCaptionText(text)
                    Keys.onReturnPressed: focus = false
                    Keys.onEnterPressed: focus = false
                }

                DialogButton {
                    text: "Style"
                    onClicked: win.styleVisible = true
                }

                IconButton {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    iconName: "trash"
                    tipText: "Delete this caption (Del)"
                    enabled: win.selectedCaption >= 0
                    onClicked: win.deleteCaption()
                }

                Item { Layout.preferredWidth: 44 }
            }
        }

        // --- status line ---
        Item {
            visible: win.hasVideo
            Layout.fillWidth: true
            Layout.preferredHeight: 26

            Label {
                anchors.centerIn: parent
                width: parent.width
                visible: win.statusText !== ""
                text: win.statusText
                color: win.noticeText !== "" ? win.accent : "#b8b8bc"
                font.pixelSize: 13
                font.family: "monospace"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }

            Label {
                anchors.centerIn: parent
                visible: win.statusText === "" && backend.duration > 0 && !trimBar.trimmingRange
                textFormat: Text.StyledText
                text: Format.fmt(trimBar.playheadSec) + " (" + Format.fmt(trimBar.endSec - trimBar.startSec) + ")"
                    + (trimBar.zoomed ? " · <font color=\"" + win.accent + "\">zoomed</font>" : "")
                    + (win.hasCaptions ? " · <font color=\"" + win.accent + "\">"
                        + captionModel.count + (captionModel.count === 1 ? " caption" : " captions")
                        + "</font>" : "")
                color: "#d6d6da"
                font.pixelSize: 13
                font.family: "monospace"
            }
        }
    }

    // --- subtle help toggle in the corner ---
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: 24
        height: 24
        radius: 12
        color: helpHover.hovered ? "#2c2c2f" : "transparent"

        Text {
            anchors.centerIn: parent
            text: "?"
            color: helpHover.hovered ? "white" : "#7a7a80"
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        HoverHandler {
            id: helpHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: win.helpVisible = !win.helpVisible
        }
    }

    // --- caption style ---
    CaptionStylePanel {
        visible: win.styleVisible
        target: captionStyle
        accent: win.accent
        accentForeground: win.accentForeground
        videoPixelHeight: backend.videoHeight > 0 ? backend.videoHeight : 1080
        onChanged: win.syncCaptions()
        onClosed: win.styleVisible = false
    }

    // --- hotkey overlay ---
    Rectangle {
        visible: win.helpVisible
        anchors.fill: parent
        color: "#000000cc"

        MouseArea {
            anchors.fill: parent
            onClicked: win.helpVisible = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: helpColumn.width + 56
            height: helpColumn.height + 48
            radius: 12
            color: "#1c1c1e"

            Column {
                id: helpColumn
                anchors.centerIn: parent
                spacing: 10

                Label {
                    text: "Keyboard shortcuts"
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    bottomPadding: 8
                }

                Repeater {
                    model: [
                        { keys: "Space", action: "Play / pause" },
                        { keys: "← / →", action: "Move playhead 1s" },
                        { keys: "Shift ← / →", action: "Move playhead 5s" },
                        { keys: "Alt ← / →", action: "Move playhead 0.2s" },
                        { keys: "Ctrl Space", action: "Trim start to playhead" },
                        { keys: "Alt Space", action: "Trim end to playhead" },
                        { keys: "Z", action: "Zoom the selection" },
                        { keys: "C", action: "Subtitle editor" },
                        { keys: "T", action: "Add a caption at the playhead" },
                        { keys: "[ / ]", action: "Caption start / end to playhead" },
                        { keys: "Del", action: "Delete the caption" },
                        { keys: "Ctrl O", action: "Open a video" },
                        { keys: "Ctrl S", action: "Export" },
                        { keys: "Q", action: "Quit" },
                        { keys: "?", action: "Show these shortcuts" }
                    ]
                    delegate: Row {
                        spacing: 18
                        Label {
                            width: 110
                            horizontalAlignment: Text.AlignRight
                            text: modelData.keys
                            color: win.accent
                            font.pixelSize: 13
                            font.family: "monospace"
                        }
                        Label {
                            text: modelData.action
                            color: "#d6d6da"
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }
    }

    // --- quit confirmation ---
    Rectangle {
        visible: win.quitConfirmVisible
        anchors.fill: parent
        color: "#000000cc"
        onVisibleChanged: {
            if (visible)
                quitExportButton.forceActiveFocus();
        }

        MouseArea {
            anchors.fill: parent
            onClicked: win.quitConfirmVisible = false
        }

        Rectangle {
            anchors.centerIn: parent
            width: quitColumn.width + 64
            height: quitColumn.height + 48
            radius: 12
            color: "#1c1c1e"

            MouseArea {
                anchors.fill: parent
            }

            Column {
                id: quitColumn
                anchors.centerIn: parent
                spacing: 8

                Label {
                    text: win.hasCaptions ? "Unexported changes" : "Unexported trim"
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Label {
                    text: (win.hasCaptions ? "Your captions haven't been exported."
                                           : "Your trim hasn't been exported.") + " Quit anyway?"
                    color: "#d6d6da"
                    font.pixelSize: 13
                    bottomPadding: 12
                }

                Row {
                    anchors.right: parent.right
                    spacing: 10

                    DialogButton {
                        id: quitCancelButton
                        text: "Cancel"
                        KeyNavigation.left: quitExportButton
                        KeyNavigation.right: quitQuitButton
                        KeyNavigation.tab: quitQuitButton
                        KeyNavigation.backtab: quitExportButton
                        onClicked: win.quitConfirmVisible = false
                    }
                    DialogButton {
                        id: quitQuitButton
                        text: "Quit"
                        KeyNavigation.left: quitCancelButton
                        KeyNavigation.right: quitExportButton
                        KeyNavigation.tab: quitExportButton
                        KeyNavigation.backtab: quitCancelButton
                        onClicked: forceQuit()
                    }
                    DialogButton {
                        id: quitExportButton
                        text: "Export"
                        primary: true
                        KeyNavigation.left: quitQuitButton
                        KeyNavigation.right: quitCancelButton
                        KeyNavigation.tab: quitCancelButton
                        KeyNavigation.backtab: quitQuitButton
                        onClicked: {
                            win.quitConfirmVisible = false;
                            exportVideo();
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: backend
        function onInfoChanged() {
            win.noticeText = "";
            noticeTimer.stop();
            // Reset priming too, or a video opened mid-prime would stay black:
            // startPriming() bails while priming is still true.
            primeFallback.stop();
            player.priming = false;
            player.primed = false;
            trimBar.zoomed = false;
            trimBar.startSec = 0;
            trimBar.endSec = backend.duration;
            trimBar.playheadSec = 0;
            win.exportedStartSec = -1;
            win.exportedEndSec = -1;
            // Captions belong to the video that was open, and their default
            // size follows the new one's height.
            captionModel.clear();
            win.selectCaption(-1);
            win.styleVisible = false;
            win.resetCaptionStyle();
            win.syncCaptions();
            win.captionRevision = 0;
            win.exportedCaptionRevision = 0;
        }
        function onExportDone(path, sidecarPath) {
            win.exportedStartSec = win.pendingExportStartSec;
            win.exportedEndSec = win.pendingExportEndSec;
            win.exportedCaptionRevision = win.pendingCaptionRevision;
            win.showNotice("Saved " + path
                + (sidecarPath !== "" ? " + " + fileName(sidecarPath) : ""));
        }
        function onExportWarning(message) {
            win.showNotice(message);
        }
        function onExportFailed(message) {
            win.showNotice("Export failed: " + message);
        }
        function onLoadError(message) {
            win.showNotice("Cannot open video: " + message);
        }
    }
}
