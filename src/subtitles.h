#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Captions and the ASS document that burns them into an export. Kept free of
// Qt Quick and of ffmpeg so both the export path and the tests can use it.
namespace subtitles {

// One caption, timed against the *source* video.
struct Cue {
    double start = 0.0;
    double end = 0.0;
    QString text;
};

// One style for every caption: they all sit bottom-centred, which is what the
// on-screen editor offers, so there is nothing per-cue to carry here.
struct Style {
    QString fontFamily = QStringLiteral("Liberation Sans");
    // Sizes are in the video's own pixels (PlayRes below), so a 720p export
    // shrinks the captions with the frame instead of leaving them oversized.
    int fontSize = 72;
    bool bold = true;
    QString textColor = QStringLiteral("#ffffff");
    QString outlineColor = QStringLiteral("#000000");
    double outlineWidth = 5.0;
    // The box replaces the outline (ASS draws one or the other), with
    // outlineWidth becoming the padding around the text.
    bool box = false;
    QString boxColor = QStringLiteral("#000000");
    int boxOpacity = 70;  // percent, 100 = fully opaque
    int marginV = 80;     // distance from the bottom edge
};

// The QML side hands cues over as [{start, end, text}] and the style as a map.
QList<Cue> cuesFromVariant(const QVariantList &list);
Style styleFromVariant(const QVariantMap &map);

// "#rrggbb" -> ASS's "&HAABBGGRR" (AA is inverted: 00 is opaque).
QString assColor(const QString &color, int opacityPercent = 100);
// Seconds -> "H:MM:SS.cc", ASS's timestamp format.
QString assTime(double seconds);
// Escape the ASS markers ({} start an override block) and fold newlines to \N.
QString assText(const QString &text);

// The cues that land inside [clipStart, clipEnd], rebased so the clip starts at
// zero and clamped to its bounds. Blank captions are dropped — an empty caption
// block in the editor is one you haven't typed into yet, not a cue.
QList<Cue> cuesForClip(const QList<Cue> &cues, double clipStart, double clipEnd);

// The .ass document for a clip. width/height are the video's displayed size and
// become PlayResX/Y; libass scales that to whatever the export's frame size is.
QString buildAss(const QList<Cue> &cues, const Style &style, int width, int height,
                 double clipStart, double clipEnd);

}  // namespace subtitles
