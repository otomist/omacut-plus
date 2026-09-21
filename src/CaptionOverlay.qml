import QtQuick

// The caption drawn where the export will burn it: bottom-centred inside the
// video's own rectangle. Sizes arrive in the video's pixels — the same units
// the .ass file uses — and are scaled to whatever size the preview is showing.
Item {
    id: root

    property string text: ""
    // Where the frame actually sits inside the preview (letterboxing and all).
    property rect videoRect: Qt.rect(0, 0, 0, 0)
    // The video's own height, which is what the sizes below are relative to.
    property int videoPixelHeight: 0
    property string fontFamily: "sans-serif"
    property int fontSize: 72
    property bool bold: true
    property color textColor: "white"
    property color outlineColor: "black"
    property real outlineWidth: 5
    property bool box: false
    property color boxColor: "black"
    property int boxOpacity: 70
    property int marginV: 80

    // ASS wraps long captions against a side margin; matching it here keeps the
    // preview's line breaks the same as the export's.
    readonly property real sideMarginRatio: 0.06
    readonly property real scaleFactor: videoPixelHeight > 0 && videoRect.height > 0
        ? videoRect.height / videoPixelHeight
        : 1
    // libass sizes a caption by its line height (ascent + descent) where Qt
    // sizes it by the em square, so the same number draws smaller in the export
    // than it would here. Folding the font's own ratio in is what keeps the
    // preview honest — and it makes a line exactly fontSize tall, which is what
    // the bottom margin is measured against.
    readonly property real emRatio: metrics.height > 0 ? metrics.font.pixelSize / metrics.height : 1
    readonly property real pixelSize: Math.max(1, fontSize * scaleFactor * emRatio)
    readonly property real outlinePx: outlineWidth * scaleFactor

    FontMetrics {
        id: metrics
        font.family: root.fontFamily
        font.bold: root.bold
        font.pixelSize: 100  // a reference size; the ratio doesn't depend on it
    }

    visible: text !== "" && videoRect.width > 0 && videoRect.height > 0

    Item {
        id: caption
        x: root.videoRect.x + root.videoRect.width * root.sideMarginRatio
        width: root.videoRect.width * (1 - 2 * root.sideMarginRatio)
        height: label.implicitHeight
        y: root.videoRect.y + root.videoRect.height - root.marginV * root.scaleFactor - height

        // ASS draws either an opaque box or an outline, never both, and the
        // outline width doubles as the box's padding.
        Rectangle {
            visible: root.box
            x: (parent.width - label.contentWidth) / 2 - root.outlinePx
            width: label.contentWidth + 2 * root.outlinePx
            y: -root.outlinePx
            height: parent.height + 2 * root.outlinePx
            color: root.boxColor
            opacity: root.boxOpacity / 100
        }

        // The outline is a ring of offset copies; libass strokes the glyphs
        // properly, but at these sizes two rings are indistinguishable from it.
        Repeater {
            model: root.box || root.outlinePx <= 0 ? [] : outlineOffsets()
            function outlineOffsets() {
                var offsets = [];
                for (var ring = 0; ring < 2; ++ring) {
                    var radius = root.outlinePx * (ring === 0 ? 1 : 0.55);
                    var steps = 12;
                    for (var i = 0; i < steps; ++i) {
                        var angle = (i / steps) * 2 * Math.PI + (ring === 0 ? 0 : Math.PI / steps);
                        offsets.push(Qt.point(Math.cos(angle) * radius, Math.sin(angle) * radius));
                    }
                }
                return offsets;
            }
            delegate: Text {
                x: modelData.x
                y: modelData.y
                width: caption.width
                text: root.text
                color: root.outlineColor
                font.family: root.fontFamily
                font.pixelSize: root.pixelSize
                font.bold: root.bold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        Text {
            id: label
            width: parent.width
            text: root.text
            color: root.textColor
            font.family: root.fontFamily
            font.pixelSize: root.pixelSize
            font.bold: root.bold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
