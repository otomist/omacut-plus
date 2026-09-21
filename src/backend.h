#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <QVector>

#include <memory>

#include "ffmpeg.h"
#include "subtitles.h"

class ThumbProvider;
class FilePicker;
class ThumbWorker;

// The bridge between QML and the ffmpeg/ffprobe layer. Holds the currently
// loaded video's info and drives thumbnail generation and export.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY infoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY infoChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY infoChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY infoChanged)
    Q_PROPERTY(int thumbCount READ thumbCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbReadyCount READ thumbReadyCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbRevision READ thumbRevision NOTIFY thumbsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeAccentChanged)
    Q_PROPERTY(QString themeAccentForeground READ themeAccentForeground NOTIFY themeAccentChanged)

public:
    explicit Backend(ThumbProvider *provider, QObject *parent = nullptr);
    explicit Backend(ThumbProvider *provider, FilePicker *filePicker,
                     QObject *parent = nullptr);
    ~Backend() override;

    QUrl source() const { return m_source; }
    double duration() const { return m_info.duration; }
    // The displayed size, so QML lays captions out over the frame it shows.
    int videoWidth() const { return m_info.displayWidth; }
    int videoHeight() const { return m_info.displayHeight; }
    int thumbCount() const { return m_thumbCount; }
    int thumbReadyCount() const { return m_thumbReadyCount; }
    int thumbRevision() const { return m_thumbRevision; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeAccentForeground() const;

    // The accent from an omarchy colors.toml, or the fallback when the file is
    // missing or holds no usable accent — which is what keeps omacut working on
    // distros without omarchy themes.
    static QString accentFromColorsFile(const QString &path, const QString &fallback);
    // "black" or "white", whichever stays legible on the given color.
    static QString foregroundFor(const QString &color);

    // Load a video (probes it, then kicks off thumbnail generation).
    Q_INVOKABLE bool load(const QUrl &url);

    // Open native desktop file dialogs.
    Q_INVOKABLE void openVideoDialog();
    Q_INVOKABLE void exportDialog(double start, double end);

    // Suggested "<name>_trimmed.mp4" target next to the source.
    Q_INVOKABLE QUrl suggestedExportUrl() const;

    // Write [start, end] (seconds) of the loaded video to dst. A non-zero
    // scaleHeight downscales the shorter side to that size. Any captions set
    // with setCaptions() that fall inside the range are burned in, and sidecar
    // also writes them next to the video as an .srt.
    Q_INVOKABLE void exportClip(const QUrl &dst, double start, double end,
                                int scaleHeight = 0, bool sidecar = false);

    // The captions to burn in, pushed from the editor whenever they change:
    // cues are [{start, end, text}] in source-video seconds, style is the one
    // look they all share (see subtitles::Style).
    Q_INVOKABLE void setCaptions(const QVariantList &cues, const QVariantMap &style);

    // Whether any caption would actually show up in [start, end] — what the UI
    // uses to tell "burning captions in" apart from a plain trim.
    Q_INVOKABLE bool hasCaptionsIn(double start, double end) const;

    // The downscale heights worth offering for a source: only ones strictly
    // below the source's shorter side, so exports never upscale.
    static QList<int> exportHeights(int width, int height);

    // Regenerate the filmstrip for [start, end] (seconds) — used by zoom.
    // The full-length strip is cached, so zooming back out restores instantly.
    Q_INVOKABLE void requestThumbs(double start, double end);

signals:
    void infoChanged();
    void thumbsChanged();
    void busyChanged();
    void statusChanged();
    void themeAccentChanged();
    void exportDone(const QString &path, const QString &sidecarPath);
    void exportFailed(const QString &message);
    // The export itself went through, but something beside it didn't.
    void exportWarning(const QString &message);
    void loadError(const QString &message);

private:
    void setBusy(bool busy);
    // The .ass file for the captions inside [start, end], or nullptr when none
    // of them show up there. It stays alive — and on disk — only as long as the
    // returned handle does.
    std::unique_ptr<QTemporaryFile> writeCaptionFile(double start, double end);
    // The .srt next to the exported video, or an empty string if it couldn't be
    // written (which is reported, never silent).
    QString writeSidecar(const QString &videoPath, const QString &document);
    void setStatus(const QString &status);
    void failExport(const QString &tmpPath, const QString &message);
    void startThumbs();
    void stopThumbs();
    void revealNextThumb();
    void wireFilePicker();
    void loadThemeAccent();
    void watchTheme();

    ThumbProvider *m_provider;
    FilePicker *m_filePicker;
    ThumbWorker *m_thumbWorker = nullptr;
    ffmpeg::VideoInfo m_info;
    QList<subtitles::Cue> m_cues;
    subtitles::Style m_captionStyle;
    QString m_path;
    QUrl m_source;
    double m_thumbStart = 0.0;
    double m_thumbLen = 0.0;
    QVector<QImage> m_fullThumbs;
    bool m_fullThumbsComplete = false;
    int m_thumbCount = 0;
    int m_thumbAvailableCount = 0;
    int m_thumbReadyCount = 0;
    int m_thumbRevision = 0;
    bool m_thumbWorkerDone = false;
    bool m_busy = false;
    QString m_status;
    QString m_themeAccent;
    QTimer m_thumbRevealTimer;
    QFileSystemWatcher m_themeWatcher;
};
