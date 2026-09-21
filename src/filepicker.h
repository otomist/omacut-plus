#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openVideo() = 0;
    // scaleHeights are the downscale choices to offer besides "Original"
    // (e.g. {1080, 720}), matched by min(width, height) of the source.
    // offerSidecar adds the choice of writing an .srt next to the video, and is
    // only worth offering when the export actually has captions in it.
    virtual void exportVideo(const QUrl &suggestedUrl, double start, double end,
                             const QList<int> &scaleHeights, bool offerSidecar) = 0;

signals:
    void openSelected(const QUrl &url);
    // scaleHeight is 0 for "Original", otherwise the chosen short-side size.
    // sidecar is true when the .srt was asked for as well.
    void exportSelected(const QUrl &url, double start, double end, int scaleHeight,
                        bool sidecar);
    void failed(const QString &message);
};
