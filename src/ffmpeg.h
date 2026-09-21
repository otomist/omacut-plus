#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <atomic>

// Thin wrappers around the ffmpeg/ffprobe command-line tools.
namespace ffmpeg {

struct VideoInfo {
    QString path;
    double duration = 0.0;  // seconds
    int width = 0;
    int height = 0;
    // The size the frame is *shown* at: phone clips carry a rotation that swaps
    // the stored width and height, and captions have to be laid out against
    // what the viewer sees.
    int displayWidth = 0;
    int displayHeight = 0;
    int rotation = 0;  // degrees, normalised to 0/90/180/270
    bool ok = false;
    QString error;
};

// Probe a file for a usable video stream and duration (runs ffprobe).
VideoInfo probe(const QString &path);

// Grab a single frame at `time` seconds, scaled to `height` px.
// Returns a null QImage on failure. If `cancel` is set and flips to true while
// the ffmpeg child is running, the child is killed and a null QImage returned.
QImage thumbnail(const QString &path, double time, int height = 90,
                 const std::atomic<bool> *cancel = nullptr);

// Build the ffmpeg argument list that writes [start, end] of src to dst.
// Cuts are frame-accurate and re-encoded with libx264/aac. A non-zero
// scaleHeight downscales so the shorter side becomes scaleHeight (1080p of a
// portrait video is 1080 wide), always preserving the aspect ratio. A non-empty
// assPath burns that subtitle file in, after any downscale so the text is drawn
// at the export's own resolution rather than resampled with the frame.
QStringList trimArgs(const QString &src, const QString &dst, double start, double end,
                     int scaleHeight = 0, const QString &assPath = QString());

// Escape a path for use inside a filtergraph argument (colons and backslashes
// in a temp directory would otherwise split the filter description).
QString escapeFilterPath(const QString &path);

// Locate a tool on PATH; returns empty string if missing.
QString toolPath(const QString &tool);

}  // namespace ffmpeg
