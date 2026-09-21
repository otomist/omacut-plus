import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The look every caption shares. Edits land straight on `target` (the style
// object Main.qml keeps) and announce themselves with changed(), which is what
// pushes the new style down to the export.
Rectangle {
    id: root
    anchors.fill: parent
    color: "#000000cc"

    property var target: null
    property color accent: "#FFD60A"
    property color accentForeground: "black"
    // The video's own height, so the sizes below read as a share of the frame
    // rather than as bare pixel counts.
    property int videoPixelHeight: 1080

    signal changed()
    signal closed()

    readonly property var swatches: ["#ffffff", "#000000", "#ffd60a", "#ff453a",
                                     "#32d74b", "#0a84ff", "#ff9f0a", "#bf5af2"]

    MouseArea {
        anchors.fill: parent
        onClicked: root.closed()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 460)
        height: Math.min(parent.height - 48, styleColumn.implicitHeight + 44)
        radius: 12
        color: "#1c1c1e"

        MouseArea {
            anchors.fill: parent
        }

        // A label above each control, so the panel stays readable at any width.
        component Field: ColumnLayout {
            id: field
            property string label: ""
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: field.label
                color: "#9a9aa0"
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        // Same flat, accent-filled look the rest of the app uses for buttons.
        component ToggleChip: Rectangle {
            id: chip
            property string label: ""
            property bool checked: false
            signal toggled()
            implicitWidth: chipLabel.implicitWidth + 24
            implicitHeight: 36
            radius: 8
            color: checked ? root.accent : "#2c2c2f"

            Label {
                id: chipLabel
                anchors.centerIn: parent
                text: chip.label
                color: chip.checked ? root.accentForeground : "white"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: chip.toggled()
            }
        }

        component ColorRow: RowLayout {
            id: colorRow
            property color value: "white"
            signal picked(color chosen)
            spacing: 6

            Repeater {
                model: root.swatches
                Rectangle {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    radius: 11
                    color: modelData
                    border.color: Qt.colorEqual(colorRow.value, modelData) ? root.accent : "#4a4a50"
                    border.width: Qt.colorEqual(colorRow.value, modelData) ? 3 : 1
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: colorRow.picked(modelData)
                    }
                }
            }
            // Typing a hex value covers whatever the swatches don't.
            TextField {
                id: hexField
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                text: colorRow.value
                font.pixelSize: 11
                font.family: "monospace"
                color: "white"
                selectByMouse: true
                // Only accept a complete hex colour; anything else snaps back,
                // so a half-typed "#ff" can't blank the caption.
                onEditingFinished: {
                    var typed = text.trim();
                    if (/^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(typed))
                        colorRow.picked(typed);
                    else
                        text = colorRow.value;
                }
            }
            // The swatches write to `value`, so the field has to follow it.
            onValueChanged: hexField.text = value
        }

        ScrollView {
            anchors.fill: parent
            anchors.margins: 22
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                id: styleColumn
                width: parent.width
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Caption style"
                        color: "white"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Esc"
                        color: "#6b6b71"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                }

                Field {
                    label: "Font"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ComboBox {
                            id: fontBox
                            Layout.fillWidth: true
                            model: Qt.fontFamilies()
                            currentIndex: Math.max(0, model.indexOf(root.target ? root.target.fontFamily : ""))
                            font.pixelSize: 12
                            onActivated: {
                                root.target.fontFamily = currentText;
                                root.changed();
                            }
                        }
                        ToggleChip {
                            label: "Bold"
                            checked: root.target ? root.target.bold : true
                            onToggled: {
                                root.target.bold = !root.target.bold;
                                root.changed();
                            }
                        }
                    }
                }

                Field {
                    label: "Size — " + (root.target ? root.target.fontSize : 0) + " px ("
                        + Math.round((root.target ? root.target.fontSize : 0)
                                     / Math.max(1, root.videoPixelHeight) * 100) + "% of the frame)"
                    Slider {
                        Layout.fillWidth: true
                        from: Math.max(8, Math.round(root.videoPixelHeight * 0.01))
                        to: Math.max(24, Math.round(root.videoPixelHeight * 0.25))
                        stepSize: 1
                        value: root.target ? root.target.fontSize : 72
                        Material.accent: root.accent
                        onMoved: {
                            root.target.fontSize = Math.round(value);
                            root.changed();
                        }
                    }
                }

                Field {
                    label: "Text colour"
                    ColorRow {
                        Layout.fillWidth: true
                        value: root.target ? root.target.textColor : "white"
                        onPicked: function (chosen) {
                            root.target.textColor = chosen;
                            root.changed();
                        }
                    }
                }

                Field {
                    label: root.target && root.target.box ? "Box colour" : "Outline colour"
                    ColorRow {
                        Layout.fillWidth: true
                        value: root.target ? (root.target.box ? root.target.boxColor
                                                              : root.target.outlineColor)
                                           : "black"
                        onPicked: function (chosen) {
                            if (root.target.box)
                                root.target.boxColor = chosen;
                            else
                                root.target.outlineColor = chosen;
                            root.changed();
                        }
                    }
                }

                Field {
                    label: (root.target && root.target.box ? "Box padding — " : "Outline width — ")
                        + (root.target ? root.target.outlineWidth : 0) + " px"
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 24
                        stepSize: 1
                        value: root.target ? root.target.outlineWidth : 5
                        Material.accent: root.accent
                        onMoved: {
                            root.target.outlineWidth = Math.round(value);
                            root.changed();
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox {
                        text: "Background box"
                        checked: root.target ? root.target.box : false
                        font.pixelSize: 12
                        Material.accent: root.accent
                        onToggled: {
                            root.target.box = checked;
                            root.changed();
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "replaces the outline"
                        color: "#6b6b71"
                        font.pixelSize: 11
                    }
                }

                Field {
                    label: "Box opacity — " + (root.target ? root.target.boxOpacity : 0) + "%"
                    visible: root.target ? root.target.box : false
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 5
                        value: root.target ? root.target.boxOpacity : 70
                        Material.accent: root.accent
                        onMoved: {
                            root.target.boxOpacity = Math.round(value);
                            root.changed();
                        }
                    }
                }

                Field {
                    label: "Distance from the bottom — " + (root.target ? root.target.marginV : 0) + " px"
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(40, Math.round(root.videoPixelHeight * 0.45))
                        stepSize: 1
                        value: root.target ? root.target.marginV : 80
                        Material.accent: root.accent
                        onMoved: {
                            root.target.marginV = Math.round(value);
                            root.changed();
                        }
                    }
                }
            }
        }
    }
}
