#include "subtitles.h"

#include <QColor>
#include <QStringList>

#include <cmath>

namespace subtitles {

namespace {
// Fallback canvas when the video's size is unknown, so a missing probe still
// produces captions at a sane size instead of none at all.
constexpr int kFallbackWidth = 1920;
constexpr int kFallbackHeight = 1080;
// Side margins, as a share of the width: what ASS wraps long captions against.
constexpr double kSideMarginRatio = 0.06;

QString styleName() { return QStringLiteral("omacut"); }
}

QList<Cue> cuesFromVariant(const QVariantList &list) {
    QList<Cue> cues;
    cues.reserve(list.size());
    for (const QVariant &item : list) {
        const QVariantMap map = item.toMap();
        Cue cue;
        cue.start = map.value(QStringLiteral("start")).toDouble();
        cue.end = map.value(QStringLiteral("end")).toDouble();
        cue.text = map.value(QStringLiteral("text")).toString();
        cues.append(cue);
    }
    return cues;
}

Style styleFromVariant(const QVariantMap &map) {
    Style style;
    const auto pick = [&map](const char *key, auto fallback) {
        const QVariant value = map.value(QLatin1String(key));
        return value.isValid() ? value : QVariant::fromValue(fallback);
    };

    const QString family = map.value(QStringLiteral("fontFamily")).toString().trimmed();
    if (!family.isEmpty())
        style.fontFamily = family;
    style.fontSize = qMax(1, pick("fontSize", style.fontSize).toInt());
    style.bold = pick("bold", style.bold).toBool();
    style.textColor = pick("textColor", style.textColor).toString();
    style.outlineColor = pick("outlineColor", style.outlineColor).toString();
    style.outlineWidth = qMax(0.0, pick("outlineWidth", style.outlineWidth).toDouble());
    style.box = pick("box", style.box).toBool();
    style.boxColor = pick("boxColor", style.boxColor).toString();
    style.boxOpacity = qBound(0, pick("boxOpacity", style.boxOpacity).toInt(), 100);
    style.marginV = qMax(0, pick("marginV", style.marginV).toInt());
    return style;
}

QString assColor(const QString &color, int opacityPercent) {
    QColor parsed = QColor::fromString(color);
    if (!parsed.isValid())
        parsed = QColor(Qt::white);
    // ASS alpha runs the other way from everyone else's: 00 is opaque.
    const int alpha = 255 - qBound(0, qRound(qBound(0, opacityPercent, 100) * 2.55), 255);
    return QStringLiteral("&H%1%2%3%4")
        .arg(alpha, 2, 16, QLatin1Char('0'))
        .arg(parsed.blue(), 2, 16, QLatin1Char('0'))
        .arg(parsed.green(), 2, 16, QLatin1Char('0'))
        .arg(parsed.red(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString assTime(double seconds) {
    if (!(seconds > 0.0) || std::isnan(seconds))
        seconds = 0.0;
    // Round to centiseconds first, so 59.999 becomes 0:01:00.00 rather than
    // 0:00:60.00 — the same trap Format.js works around on the QML side.
    qint64 cs = qRound64(seconds * 100.0);
    const qint64 hours = cs / 360000;
    cs -= hours * 360000;
    const qint64 minutes = cs / 6000;
    cs -= minutes * 6000;
    const qint64 wholeSeconds = cs / 100;
    cs -= wholeSeconds * 100;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(wholeSeconds, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

QString assText(const QString &text) {
    QString out = text;
    // Backslash first, or the escapes added below would be escaped again.
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('{'), QStringLiteral("\\{"));
    out.replace(QLatin1Char('}'), QStringLiteral("\\}"));
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\\N"));
    out.replace(QLatin1Char('\n'), QStringLiteral("\\N"));
    out.replace(QLatin1Char('\r'), QStringLiteral("\\N"));
    return out;
}

QList<Cue> cuesForClip(const QList<Cue> &cues, double clipStart, double clipEnd) {
    QList<Cue> clipped;
    for (const Cue &cue : cues) {
        if (cue.text.trimmed().isEmpty())
            continue;
        const double start = qMax(cue.start, clipStart);
        const double end = qMin(cue.end, clipEnd);
        if (end - start <= 0.0)
            continue;
        clipped.append({start - clipStart, end - clipStart, cue.text});
    }
    return clipped;
}

QString srtTime(double seconds) {
    if (!(seconds > 0.0) || std::isnan(seconds))
        seconds = 0.0;
    qint64 ms = qRound64(seconds * 1000.0);
    const qint64 hours = ms / 3600000;
    ms -= hours * 3600000;
    const qint64 minutes = ms / 60000;
    ms -= minutes * 60000;
    const qint64 wholeSeconds = ms / 1000;
    ms -= wholeSeconds * 1000;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(wholeSeconds, 2, 10, QLatin1Char('0'))
        .arg(ms, 3, 10, QLatin1Char('0'));
}

QString buildSrt(const QList<Cue> &cues, double clipStart, double clipEnd) {
    const QList<Cue> clipped = cuesForClip(cues, clipStart, clipEnd);
    if (clipped.isEmpty())
        return {};

    QString out;
    int index = 0;
    for (const Cue &cue : clipped) {
        // SubRip has no escaping: the text goes in as typed, and a blank line
        // ends each entry.
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                   .arg(++index)
                   .arg(srtTime(cue.start), srtTime(cue.end), cue.text);
    }
    return out;
}

QString buildAss(const QList<Cue> &cues, const Style &style, int width, int height,
                 double clipStart, double clipEnd) {
    const int playResX = width > 0 ? width : kFallbackWidth;
    const int playResY = height > 0 ? height : kFallbackHeight;
    const int marginH = qMax(10, qRound(playResX * kSideMarginRatio));

    // BorderStyle 3 is ASS's opaque box, which replaces the outline and reads
    // the box colour off BackColour; 1 is the plain outline everyone expects.
    const int borderStyle = style.box ? 3 : 1;
    const QString outlineColour = style.box ? assColor(style.boxColor, style.boxOpacity)
                                            : assColor(style.outlineColor);
    const QString backColour = style.box ? assColor(style.boxColor, style.boxOpacity)
                                         : assColor(style.outlineColor);

    QStringList lines;
    lines << QStringLiteral("[Script Info]")
          << QStringLiteral("; Written by omacut")
          << QStringLiteral("ScriptType: v4.00+")
          << QStringLiteral("WrapStyle: 0")
          // Keep the border proportional when libass renders onto a frame that
          // isn't PlayRes — a 720p export of a 4K source, say.
          << QStringLiteral("ScaledBorderAndShadow: yes")
          << QStringLiteral("PlayResX: %1").arg(playResX)
          << QStringLiteral("PlayResY: %1").arg(playResY)
          << QString()
          << QStringLiteral("[V4+ Styles]")
          << QStringLiteral("Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
                            "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                            "Alignment, MarginL, MarginR, MarginV, Encoding")
          // Alignment 2 is bottom-centre.
          << QStringLiteral("Style: %1,%2,%3,%4,%4,%5,%6,%7,0,0,0,100,100,0,0,%8,%9,0,2,%10,%10,%11,1")
                 .arg(styleName(), style.fontFamily)
                 .arg(style.fontSize)
                 .arg(assColor(style.textColor), outlineColour, backColour)
                 .arg(style.bold ? -1 : 0)
                 .arg(borderStyle)
                 .arg(QString::number(style.outlineWidth, 'f', 1))
                 .arg(marginH)
                 .arg(style.marginV)
          << QString()
          << QStringLiteral("[Events]")
          << QStringLiteral("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                            "Effect, Text");

    for (const Cue &cue : cuesForClip(cues, clipStart, clipEnd)) {
        lines << QStringLiteral("Dialogue: 0,%1,%2,%3,,0,0,0,,%4")
                     .arg(assTime(cue.start), assTime(cue.end), styleName(), assText(cue.text));
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace subtitles
