#include "ffmpeg.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace ffmpeg {

namespace {
// Upper bound on a synchronous probe so a hung/unresponsive file (e.g. a stalled
// network mount) can't freeze the caller — load() runs probe() on the UI thread.
constexpr int kProbeTimeoutMs = 15000;
// Poll granularity while waiting on a thumbnail child, so cancellation is prompt.
constexpr int kThumbPollMs = 50;

// ffprobe reports rotation either as stream side data or, on older files, as a
// "rotate" tag — and either one can come back as a string.
int rotationOf(const QJsonObject &stream) {
    double degrees = 0.0;
    const QJsonArray sideData = stream.value("side_data_list").toArray();
    for (const QJsonValue &entry : sideData) {
        const QJsonValue rotation = entry.toObject().value("rotation");
        if (rotation.isUndefined())
            continue;
        degrees = rotation.isString() ? rotation.toString().toDouble() : rotation.toDouble();
    }
    if (degrees == 0.0)
        degrees = stream.value("tags").toObject().value("rotate").toString().toDouble();

    int normalised = qRound(degrees) % 360;
    if (normalised < 0)
        normalised += 360;
    // Anything that isn't a quarter turn can't swap the sides, so treat it as
    // upright rather than guessing.
    return (normalised % 90 == 0) ? normalised : 0;
}
}

QString toolPath(const QString &tool) {
    return QStandardPaths::findExecutable(tool);
}

VideoInfo probe(const QString &path) {
    VideoInfo info;
    info.path = path;

    const QString ffprobe = toolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        info.error = "`ffprobe` was not found on your PATH. Install ffmpeg.";
        return info;
    }

    QProcess proc;
    proc.start(ffprobe, {
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        "-select_streams", "v:0",
        path,
    });
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(-1);
        info.error = "ffprobe timed out reading this file.";
        return info;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        info.error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (info.error.isEmpty())
            info.error = "ffprobe failed.";
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonObject root = doc.object();
    const QJsonArray streams = root.value("streams").toArray();
    if (streams.isEmpty()) {
        info.error = "No video stream found in this file.";
        return info;
    }

    const QJsonObject stream = streams.first().toObject();

    info.width = stream.value("width").toInt();
    info.height = stream.value("height").toInt();
    info.rotation = rotationOf(stream);
    const bool quarterTurn = info.rotation == 90 || info.rotation == 270;
    info.displayWidth = quarterTurn ? info.height : info.width;
    info.displayHeight = quarterTurn ? info.width : info.height;

    // Duration can live on the stream or on the container.
    QString durationStr = stream.value("duration").toString();
    if (durationStr.isEmpty())
        durationStr = root.value("format").toObject().value("duration").toString();
    if (durationStr.isEmpty()) {
        info.error = "Could not determine the video duration.";
        return info;
    }

    info.duration = durationStr.toDouble();

    info.ok = info.duration > 0.0;
    if (!info.ok)
        info.error = "Video has a zero or invalid duration.";
    return info;
}

QImage thumbnail(const QString &path, double time, int height,
                 const std::atomic<bool> *cancel) {
    const QString ffmpeg = toolPath("ffmpeg");
    if (ffmpeg.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffmpeg, {
        "-loglevel", "error",
        "-ss", QString::number(qMax(time, 0.0), 'f', 3),
        "-i", path,
        "-frames:v", "1",
        "-vf", QString("scale=-1:%1").arg(height),
        "-f", "image2pipe",
        "-vcodec", "mjpeg",
        "pipe:1",
    });

    // Poll instead of waitForFinished(-1) so a cancel request can kill the child
    // promptly — otherwise the std::future destructor in ThumbWorker would block
    // the UI thread until ffmpeg finishes on its own.
    while (!proc.waitForFinished(kThumbPollMs)) {
        if (proc.state() == QProcess::NotRunning)
            break;  // failed to start, or exited between polls
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            proc.kill();
            proc.waitForFinished(-1);
            return {};
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};

    const QByteArray data = proc.readAllStandardOutput();
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

QString escapeFilterPath(const QString &path) {
    static const QString specials = QStringLiteral("\\':[],;");
    QString escaped;
    escaped.reserve(path.size());
    for (const QChar ch : path) {
        if (specials.contains(ch))
            escaped += QLatin1Char('\\');
        escaped += ch;
    }
    return escaped;
}

QStringList trimArgs(const QString &src, const QString &dst, double start, double end,
                     int scaleHeight, const QString &assPath) {
    // Machine-readable progress on stdout (errors stay on stderr), so the UI
    // can show how far along the encode is.
    QStringList args = {"-y", "-loglevel", "error", "-progress", "pipe:1"};
    // -ss before -i seeks fast; -t gives the output duration. +faststart puts
    // the moov atom up front so shared clips start playing before they finish
    // downloading.
    args << "-ss" << QString::number(start, 'f', 3)
         << "-i" << src
         << "-t" << QString::number(qMax(end - start, 0.0), 'f', 3);
    // Cap the shorter side, judged on the decoded (rotation-applied) frame, so
    // portrait and landscape both keep their aspect ratio. -2 keeps the other
    // side divisible by two, which libx264 requires.
    QStringList filters;
    if (scaleHeight > 0)
        filters << QString("scale='if(gt(iw,ih),-2,%1)':'if(gt(iw,ih),%1,-2)'").arg(scaleHeight);
    // Captions come last: libass then draws them at the output resolution, so
    // a downscaled export gets crisp text instead of resampled text.
    if (!assPath.isEmpty())
        filters << QString("subtitles=filename=%1").arg(escapeFilterPath(assPath));
    if (!filters.isEmpty())
        args << "-vf" << filters.join(QLatin1Char(','));
    args << "-c:v" << "libx264" << "-preset" << "veryfast"
         << "-crf" << "18" << "-c:a" << "aac"
         << "-movflags" << "+faststart"
         << dst;
    return args;
}

}  // namespace ffmpeg
