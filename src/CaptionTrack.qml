import QtQuick

// The caption lane under the filmstrip: one block per caption, dragged to move
// it and grabbed by an edge to retime it. The geometry is handed in from the
// trim bar so a block always lines up with the frames it covers, zoom included.
Item {
    id: root
    implicitHeight: 34

    property var captions: null  // ListModel of {startSec, endSec, text}
    property real durationSec: 0
    property real windowStart: 0
    property real windowEnd: 0
    property real trackX: 0
    property real trackW: 0
    property real playheadSec: 0
    property int selectedIndex: -1
    property color accent: "#FFD60A"
    property color accentForeground: "black"
    // True while a block is being dragged, so the player leaves the UI alone.
    property bool interacting: false

    readonly property real windowLen: Math.max(windowEnd - windowStart, 0.001)
    readonly property real edgeGrabW: 10
    readonly property real minLenSec: 0.2

    signal retimed(int index, real start, real end)
    signal picked(int index)
    signal addRequested(real seconds)

    function xForTime(t) {
        if (durationSec <= 0)
            return trackX;
        return trackX + ((t - windowStart) / windowLen) * trackW;
    }
    function timeForX(x) {
        if (trackW <= 0 || durationSec <= 0)
            return 0;
        var f = (x - trackX) / trackW;
        return windowStart + Math.max(0, Math.min(1, f)) * windowLen;
    }

    Rectangle {
        id: lane
        x: root.trackX
        width: root.trackW
        y: 0
        height: root.height
        radius: 6
        color: "#161618"
        border.color: "#26262a"
        border.width: 1
        clip: true

        // Empty space: click clears the selection, double-click drops a caption
        // right where you clicked.
        MouseArea {
            anchors.fill: parent
            onClicked: root.picked(-1)
            onDoubleClicked: function (mouse) {
                root.addRequested(root.timeForX(mouse.x + lane.x));
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !root.captions || root.captions.count === 0
            text: "No captions yet — press T to add one at the playhead"
            color: "#6b6b71"
            font.pixelSize: 12
        }

        // The playhead, so you can see which caption is on screen right now.
        Rectangle {
            visible: root.durationSec > 0
                && root.playheadSec >= root.windowStart && root.playheadSec <= root.windowEnd
            x: root.xForTime(root.playheadSec) - lane.x - 1
            width: 2
            height: lane.height
            color: "#ffffff66"
        }

        Repeater {
            model: root.captions

            Rectangle {
                id: block
                readonly property bool selected: index === root.selectedIndex
                readonly property real startX: root.xForTime(model.startSec) - lane.x
                readonly property real endX: root.xForTime(model.endSec) - lane.x

                x: startX
                width: Math.max(4, endX - startX)
                y: 3
                height: lane.height - 6
                radius: 5
                color: selected ? root.accent : "#3a3a3f"
                border.color: selected ? Qt.lighter(root.accent, 1.3) : "#4a4a50"
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    text: model.text === "" ? "…" : model.text
                    color: block.selected ? root.accentForeground : "#d6d6da"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                // One press decides what the drag does — the edges retime, the
                // middle slides the whole caption — mirroring the trim handles.
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    property int mode: 0  // 0 none, 1 start, 2 end, 3 move
                    property real grabOffset: 0

                    function zoneAt(x) {
                        if (block.width > 3 * root.edgeGrabW) {
                            if (x <= root.edgeGrabW)
                                return 1;
                            if (x >= block.width - root.edgeGrabW)
                                return 2;
                        }
                        return 3;
                    }

                    cursorShape: (containsMouse && zoneAt(mouseX) !== 3) ? Qt.SizeHorCursor
                                                                        : Qt.OpenHandCursor

                    onPressed: function (mouse) {
                        mode = zoneAt(mouse.x);
                        grabOffset = root.timeForX(mouse.x + block.x + lane.x) - model.startSec;
                        root.interacting = true;
                        root.picked(index);
                    }
                    onReleased: {
                        mode = 0;
                        root.interacting = false;
                    }
                    onPositionChanged: function (mouse) {
                        if (mode === 0 || root.durationSec <= 0)
                            return;
                        var t = root.timeForX(mouse.x + block.x + lane.x);
                        if (mode === 1) {
                            root.retimed(index,
                                         Math.max(0, Math.min(t, model.endSec - root.minLenSec)),
                                         model.endSec);
                        } else if (mode === 2) {
                            root.retimed(index, model.startSec,
                                         Math.min(root.durationSec,
                                                  Math.max(t, model.startSec + root.minLenSec)));
                        } else {
                            var len = model.endSec - model.startSec;
                            var start = Math.max(0, Math.min(t - grabOffset, root.durationSec - len));
                            root.retimed(index, start, start + len);
                        }
                    }
                }
            }
        }
    }
}
