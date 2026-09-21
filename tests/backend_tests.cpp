#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "backend.h"
#include "filepicker.h"
#include "subtitles.h"
#include "thumbprovider.h"
#include "thumbworker.h"

class FakeFilePicker : public FilePicker {
    Q_OBJECT

public:
    int openCount = 0;
    int exportCount = 0;
    QUrl lastSuggestedUrl;
    double lastStart = 0;
    double lastEnd = 0;
    QList<int> lastScaleHeights;
    bool lastOfferedSidecar = false;

    void openVideo() override { ++openCount; }

    void exportVideo(const QUrl &suggestedUrl, double start, double end,
                     const QList<int> &scaleHeights, bool offerSidecar) override {
        ++exportCount;
        lastSuggestedUrl = suggestedUrl;
        lastStart = start;
        lastEnd = end;
        lastScaleHeights = scaleHeights;
        lastOfferedSidecar = offerSidecar;
    }
};

class EnvVarGuard {
public:
    explicit EnvVarGuard(const char *name)
        : m_name(name), m_oldValue(qgetenv(name)), m_hadValue(qEnvironmentVariableIsSet(name)) {}

    ~EnvVarGuard() {
        if (m_hadValue)
            qputenv(m_name.constData(), m_oldValue);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    QByteArray m_oldValue;
    bool m_hadValue;
};

class ShortcutBackend : public QObject {
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
    explicit ShortcutBackend(QUrl source, double duration, QObject *parent = nullptr)
        : QObject(parent), m_source(std::move(source)), m_duration(duration) {}

    QUrl source() const { return m_source; }
    double duration() const { return m_duration; }
    int videoWidth() const { return 720; }
    int videoHeight() const { return 1280; }
    int thumbCount() const { return 0; }
    int thumbReadyCount() const { return 0; }
    int thumbRevision() const { return 0; }
    bool busy() const { return false; }
    QString status() const { return {}; }
    QString themeAccent() const { return QStringLiteral("#FFD60A"); }
    QString themeAccentForeground() const { return QStringLiteral("black"); }

    Q_INVOKABLE bool load(const QUrl &) { return false; }
    Q_INVOKABLE void openVideoDialog() { ++openCount; }
    Q_INVOKABLE void exportDialog(double start, double end) {
        ++exportCount;
        lastStart = start;
        lastEnd = end;
    }
    Q_INVOKABLE QUrl suggestedExportUrl() const { return {}; }
    Q_INVOKABLE void exportClip(const QUrl &, double, double, int, bool) {}
    Q_INVOKABLE void setCaptions(const QVariantList &cues, const QVariantMap &style) {
        ++captionSyncCount;
        lastCues = cues;
        lastCaptionStyle = style;
    }
    Q_INVOKABLE void requestThumbs(double start, double end) {
        ++thumbRequestCount;
        lastThumbStart = start;
        lastThumbEnd = end;
    }

    void announceInfo() { emit infoChanged(); }
    void announceExportDone() {
        emit exportDone(QStringLiteral("/tmp/exported.mp4"), QString());
    }

    int openCount = 0;
    int exportCount = 0;
    double lastStart = 0;
    double lastEnd = 0;
    int thumbRequestCount = 0;
    double lastThumbStart = 0;
    double lastThumbEnd = 0;
    int captionSyncCount = 0;
    QVariantList lastCues;
    QVariantMap lastCaptionStyle;

signals:
    void infoChanged();
    void thumbsChanged();
    void busyChanged();
    void statusChanged();
    void themeAccentChanged();
    void exportDone(const QString &path, const QString &sidecarPath);
    void exportFailed(const QString &message);
    void exportWarning(const QString &message);
    void loadError(const QString &message);

private:
    QUrl m_source;
    double m_duration;
};

// Finds a DialogButton by its label ("primary" tells them apart from Labels).
static QQuickItem *dialogButton(QQuickWindow *window, const QString &text) {
    const auto items = window->findChildren<QQuickItem *>();
    for (QQuickItem *item : items) {
        if (item->property("primary").isValid() && item->property("text").toString() == text)
            return item;
    }
    return nullptr;
}

static QPoint itemCenter(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

static QString mainQmlPath() {
    return QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(
        QStringLiteral("../src/Main.qml"));
}

// Loads Main.qml against a stub backend and keeps the engine alive for as long
// as the window is in use, so each shortcut test is just the key presses.
class QmlHarness {
public:
    explicit QmlHarness(ShortcutBackend &backend) {
        m_engine.addImageProvider(QStringLiteral("thumbs"), new ThumbProvider);
        m_engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        m_engine.load(QUrl::fromLocalFile(mainQmlPath()));
        if (!m_engine.rootObjects().isEmpty())
            m_window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
    }

    QQuickWindow *window() const { return m_window; }
    QQmlApplicationEngine &engine() { return m_engine; }
    QQuickItem *trimBar() const {
        return m_window ? m_window->findChild<QQuickItem *>(QStringLiteral("trimBar")) : nullptr;
    }

private:
    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
};

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void openDialogDelegatesToFilePicker();
    void pickerSelectionLoadsVideo();
    void thumbnailSlotsAreExposedImmediately();
    void thumbProviderUsesRevisionPrefixedIds();
    void thumbProviderScalesHeightOnlyRequests();
    void thumbnailWorkerStopsBlockedJobs();
    void exportDialogDelegatesSuggestedUrlAndRange();
    void suggestedExportUrlAlwaysUsesMp4();
    void exportClipWritesMp4();
    void exportClipCanReplaceSourceFile();
    void exportZeroLengthClipFails();
    void exportRefusesRewrittenPathOverExistingFile();
    void exportStartFailureClearsBusy();
    void failedExportPreservesExistingFile();
    void qmlDoesNotCreateAudioOutputWithoutVideo();
    void qmlShortcutsTriggerBackendActions();
    void qmlArrowKeysMoveThePlayhead();
    void qmlSpaceChordsSetTheTrimEdges();
    void qmlZoomFocusesTheSelection();
    void qmlQuitConfirmsUnexportedTrim();
    void captionAssCarriesTheStyleAndRebasesTheClip();
    void captionAssEscapesTheAssMarkers();
    void exportClipBurnsCaptionsIn();
    void srtSidecarNumbersAndRebasesTheCues();
    void exportOffersTheSidecarOnlyWhenCaptionsAreInTheClip();
    void exportClipWritesTheSrtSidecar();
    void qmlCaptionEditorAddsRetimesAndTypes();
    void qmlCaptionsCountAsUnexportedWork();
    void trimArgsReencodeForPreciseCuts();
    void trimArgsScaleTheShorterSide();
    void trimArgsBurnCaptionsAfterTheDownscale();
    void exportHeightsNeverUpscale();
    void themeAccentReadsOmarchyColors();
    void themeAccentForegroundKeepsContrast();

private:
    QUrl videoUrl() const { return QUrl::fromLocalFile(m_videoPath); }
    QString makeSolidVideo(const QString &name, const QString &size) const;
    QUrl exportedWith(Backend &backend, const QVariantList &cues, const QString &name);
    QString formatName(const QString &path) const;
    void waitForBackgroundWork(Backend &backend);
    bool installBrokenFfmpeg(const QString &dirPath);

    QTemporaryDir m_dir;
    QString m_videoPath;
};

void BackendTests::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Material"));

    QVERIFY2(m_dir.isValid(), "temporary directory is valid");
    m_videoPath = m_dir.filePath(QStringLiteral("clip.mp4"));

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    QVERIFY2(!ffmpeg.isEmpty(), "ffmpeg is available");

    QProcess proc;
    proc.start(ffmpeg, {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"),
        QStringLiteral("lavfi"),
        QStringLiteral("-i"),
        QStringLiteral("testsrc=size=32x32:rate=1:duration=1"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-y"),
        m_videoPath,
    });
    QVERIFY2(proc.waitForFinished(10000), qPrintable(QString::fromUtf8(proc.readAll())));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);
    QVERIFY(QFileInfo::exists(m_videoPath));
}

void BackendTests::waitForBackgroundWork(Backend &backend) {
    QTRY_VERIFY_WITH_TIMEOUT(backend.status().isEmpty(), 10000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QString BackendTests::formatName(const QString &path) const {
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffprobe, {
        QStringLiteral("-v"),
        QStringLiteral("error"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=format_name"),
        QStringLiteral("-of"),
        QStringLiteral("default=noprint_wrappers=1:nokey=1"),
        path,
    });
    if (!proc.waitForFinished(10000))
        return {};
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

// Drop an "ffmpeg" into dirPath that always fails to start (its shebang points
// nowhere), for tests that prepend dirPath to PATH.
bool BackendTests::installBrokenFfmpeg(const QString &dirPath) {
    QFile fake(QDir(dirPath).filePath(QStringLiteral("ffmpeg")));
    if (!fake.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    fake.write("#!/definitely/missing/omacut-ffmpeg\n");
    fake.close();
    return fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner);
}

void BackendTests::openDialogDelegatesToFilePicker() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    backend.openVideoDialog();
    backend.openVideoDialog();

    QCOMPARE(picker->openCount, 2);
}

void BackendTests::pickerSelectionLoadsVideo() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy infoSpy(&backend, &Backend::infoChanged);

    emit picker->openSelected(videoUrl());

    QCOMPARE(infoSpy.count(), 1);
    QCOMPARE(backend.source(), videoUrl());
    QVERIFY(backend.duration() > 0);
    waitForBackgroundWork(backend);
}

void BackendTests::thumbnailSlotsAreExposedImmediately() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy thumbsSpy(&backend, &Backend::thumbsChanged);

    QVERIFY(backend.load(videoUrl()));

    QVERIFY(backend.thumbCount() > 0);
    QCOMPARE(backend.thumbReadyCount(), 0);
    waitForBackgroundWork(backend);
    QCOMPARE(backend.thumbReadyCount(), backend.thumbCount());
    QVERIFY(thumbsSpy.count() > 2);
}

void BackendTests::thumbProviderUsesRevisionPrefixedIds() {
    ThumbProvider provider;
    provider.setImages(QVector<QImage>(2));

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    provider.setImage(1, image);

    QSize size;
    QVERIFY(provider.requestImage(QStringLiteral("4/0"), &size, QSize()).isNull());
    QVERIFY(!provider.requestImage(QStringLiteral("4/1"), &size, QSize()).isNull());
    QCOMPARE(size, image.size());
}

void BackendTests::thumbProviderScalesHeightOnlyRequests() {
    ThumbProvider provider;
    provider.setImages(QVector<QImage>(1));

    QImage image(200, 100, QImage::Format_RGB32);
    image.fill(Qt::red);
    provider.setImage(0, image);

    QSize originalSize;
    const QImage scaled = provider.requestImage(QStringLiteral("1/0"), &originalSize, QSize(0, 50));

    QCOMPARE(originalSize, image.size());
    QCOMPARE(scaled.size(), QSize(100, 50));
}

void BackendTests::thumbnailWorkerStopsBlockedJobs() {
    const QString sleepBin = QStandardPaths::findExecutable(QStringLiteral("sleep"));
    QVERIFY2(!sleepBin.isEmpty(), "sleep is available");

    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    const QString fakeFfmpeg = pathDir.filePath(QStringLiteral("ffmpeg"));
    QFile fake(fakeFfmpeg);
    QVERIFY(fake.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fake.write(QStringLiteral("#!/bin/sh\nexec \"%1\" 30\n").arg(sleepBin).toUtf8());
    fake.close();
    QVERIFY(fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner));

    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()));

    ThumbWorker worker(QStringLiteral("unused.mp4"), 0.0, 60.0, 4);
    worker.start();
    QTest::qWait(100);

    QElapsedTimer elapsed;
    elapsed.start();
    worker.requestStop();
    const bool stopped = worker.wait(2000);
    const qint64 elapsedMs = elapsed.elapsed();
    if (!stopped) {
        worker.terminate();
        worker.wait(2000);
    }

    QVERIFY2(stopped, qPrintable(QStringLiteral("worker did not stop within 2000 ms")));
    QVERIFY2(elapsedMs < 1500,
             qPrintable(QStringLiteral("worker stop took %1 ms").arg(elapsedMs)));
}

void BackendTests::exportDialogDelegatesSuggestedUrlAndRange() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);
    backend.exportDialog(0.25, 0.75);

    QCOMPARE(picker->exportCount, 1);
    QCOMPARE(picker->lastSuggestedUrl,
             QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("clip_trimmed.mp4"))));
    QCOMPARE(picker->lastStart, 0.25);
    QCOMPARE(picker->lastEnd, 0.75);
}

void BackendTests::suggestedExportUrlAlwaysUsesMp4() {
    const QString renamedSource = m_dir.filePath(QStringLiteral("renamed-source.webm"));
    QVERIFY(QFile::copy(m_videoPath, renamedSource));

    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(QUrl::fromLocalFile(renamedSource)));
    waitForBackgroundWork(backend);

    QCOMPARE(backend.suggestedExportUrl(),
             QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("renamed-source_trimmed.mp4"))));
}

void BackendTests::exportClipWritesMp4() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);
    QStringList statuses;
    connect(&backend, &Backend::statusChanged, [&backend, &statuses] {
        statuses << backend.status();
    });

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const QString selectedPath = m_dir.filePath(QStringLiteral("actual-export.webm"));
    const QString mp4Path = m_dir.filePath(QStringLiteral("actual-export.mp4"));
    backend.exportClip(QUrl::fromLocalFile(selectedPath), 0.0, 1.0);

    QVERIFY(backend.busy());
    QCOMPARE(backend.status(), QStringLiteral("Exporting 0%"));
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() + failedSpy.count() > 0, 20000);

    QCOMPARE(failedSpy.count(), 0);

    // ffmpeg's -progress stream drove the status to a completed percentage.
    QVERIFY2(statuses.contains(QStringLiteral("Exporting 100%")),
             qPrintable(statuses.join(QStringLiteral(" | "))));
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(0).toString(), mp4Path);
    QVERIFY(!backend.busy());
    QVERIFY(QFileInfo::exists(mp4Path));
    QVERIFY(!QFileInfo::exists(selectedPath));
    QVERIFY(ffmpeg::probe(mp4Path).ok);
    QVERIFY2(formatName(mp4Path).contains(QStringLiteral("mp4")),
             qPrintable(formatName(mp4Path)));
}

void BackendTests::exportClipCanReplaceSourceFile() {
    const QString sourcePath = m_dir.filePath(QStringLiteral("replace-source.mp4"));
    QVERIFY(QFile::copy(m_videoPath, sourcePath));

    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(QUrl::fromLocalFile(sourcePath)));
    waitForBackgroundWork(backend);

    backend.exportClip(QUrl::fromLocalFile(sourcePath), 0.0, 1.0);

    QVERIFY(backend.busy());
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() + failedSpy.count() > 0, 20000);

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(0).toString(), sourcePath);
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(ffmpeg::probe(sourcePath).ok);
    QVERIFY2(formatName(sourcePath).contains(QStringLiteral("mp4")),
             qPrintable(formatName(sourcePath)));
    QVERIFY(!QFileInfo::exists(sourcePath + QStringLiteral(".omacut-part.mp4")));
}

void BackendTests::exportZeroLengthClipFails() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const QString outPath = m_dir.filePath(QStringLiteral("empty-range.mp4"));
    backend.exportClip(QUrl::fromLocalFile(outPath), 0.5, 0.5);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(doneSpy.count(), 0);
    QVERIFY(!backend.busy());
    QVERIFY(!QFileInfo::exists(outPath));
}

void BackendTests::exportRefusesRewrittenPathOverExistingFile() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    // The dialog confirmed "rewrite-target.webm"; forcing the .mp4 suffix
    // would land on this existing file the user was never asked about.
    const QString selectedPath = m_dir.filePath(QStringLiteral("rewrite-target.webm"));
    const QString mp4Path = m_dir.filePath(QStringLiteral("rewrite-target.mp4"));
    const QByteArray original("original contents");
    {
        QFile existing(mp4Path);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        existing.write(original);
        existing.close();
    }

    backend.exportClip(QUrl::fromLocalFile(selectedPath), 0.0, 1.0);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(doneSpy.count(), 0);
    QVERIFY(!backend.busy());

    QFile check(mp4Path);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), original);
}

void BackendTests::exportStartFailureClearsBusy() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    QVERIFY(installBrokenFfmpeg(pathDir.path()));

    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()) + ':' + qgetenv("PATH"));

    backend.exportClip(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("failed.mp4"))),
                       0.0, 1.0);

    QVERIFY(backend.busy());
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
    QVERIFY(!backend.busy());
    QVERIFY(backend.status().isEmpty());
    QVERIFY(!failedSpy.first().at(0).toString().isEmpty());
}

void BackendTests::failedExportPreservesExistingFile() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    // A pre-existing destination file that a failed export must not clobber.
    const QString outPath = m_dir.filePath(QStringLiteral("keep-me.mp4"));
    const QByteArray original("original contents");
    {
        QFile existing(outPath);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        existing.write(original);
        existing.close();
    }

    // Force ffmpeg to fail to start, the same way exportStartFailureClearsBusy does.
    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    QVERIFY(installBrokenFfmpeg(pathDir.path()));

    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()) + ':' + qgetenv("PATH"));

    backend.exportClip(QUrl::fromLocalFile(outPath), 0.0, 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);

    // The original file survives untouched, and no temp part file is left behind.
    QFile check(outPath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), original);
    QVERIFY(!QFileInfo::exists(outPath + QStringLiteral(".omacut-part.mp4")));
}

void BackendTests::qmlDoesNotCreateAudioOutputWithoutVideo() {
    ShortcutBackend backend(QUrl(), 0.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QVERIFY(harness.window()->property("audioOutputReady").isValid());
    QCOMPARE(harness.window()->property("audioOutputReady").toBool(), false);
}

void BackendTests::qmlShortcutsTriggerBackendActions() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            1.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 1, 3000);

    QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(backend.openCount, 1, 3000);

    // ? toggles the hotkey overlay, and Escape closes it again.
    QTest::keyClick(window, Qt::Key_Question);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("helpVisible").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("helpVisible").toBool(), false, 3000);
    QCOMPARE(backend.openCount, 1);
}

void BackendTests::qmlArrowKeysMoveThePlayhead() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    // The trim only spans the video once the backend reports what it loaded.
    backend.announceInfo();
    QQuickItem *trimBar = harness.trimBar();
    QVERIFY(trimBar);
    QCOMPARE(trimBar->property("endSec").toDouble(), 20.0);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 1.0, 3000);

    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 6.0, 3000);

    QTest::keyClick(window, Qt::Key_Right, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 6.2, 3000);

    QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 6.0, 3000);

    QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 1.0, 3000);

    // Seeking never leaves the trim, so this stops at the start instead of -4.
    QTest::keyClick(window, Qt::Key_Left);
    QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 0.0, 3000);

    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("startSec").toDouble(), 5.0, 3000);
    QCOMPARE(trimBar->property("endSec").toDouble(), 20.0);
}

void BackendTests::qmlSpaceChordsSetTheTrimEdges() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    backend.announceInfo();
    QQuickItem *trimBar = harness.trimBar();
    QVERIFY(trimBar);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    // Park the playhead at 15 s and pull the end in to it.
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 15.0, 3000);
    QTest::keyClick(window, Qt::Key_Space, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("endSec").toDouble(), 15.0, 3000);

    // Same for the start, at 5 s.
    QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 5.0, 3000);
    QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("startSec").toDouble(), 5.0, 3000);

    // The edges never cross: pulling the end onto the start stops 0.1 s past it.
    QTest::keyClick(window, Qt::Key_Space, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("endSec").toDouble(), 5.1, 3000);
    QCOMPARE(trimBar->property("startSec").toDouble(), 5.0);
}

void BackendTests::qmlZoomFocusesTheSelection() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    backend.announceInfo();
    QQuickItem *trimBar = harness.trimBar();
    QVERIFY(trimBar);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    // Trim to 5..15, then zoom: the selection fills 80% of the track, so the
    // window stretches an extra eighth of the selection on each side.
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("startSec").toDouble(), 5.0, 3000);
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("endSec").toDouble(), 15.0, 3000);

    QTest::keyClick(window, Qt::Key_Z);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("zoomed").toBool(), true, 3000);
    QCOMPARE(trimBar->property("viewStartSec").toDouble(), 3.75);
    QCOMPARE(trimBar->property("viewEndSec").toDouble(), 16.25);

    // The filmstrip regenerates for the window, so the thumbs match the zoom.
    QCOMPARE(backend.thumbRequestCount, 1);
    QCOMPARE(backend.lastThumbStart, 3.75);
    QCOMPARE(backend.lastThumbEnd, 16.25);

    // Tighten the trim while zoomed: end to the playhead at 10 s.
    QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("endSec").toDouble(), 10.0, 3000);

    // The selection changed since the zoom, so Z zooms again instead of out.
    QTest::keyClick(window, Qt::Key_Z);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("viewStartSec").toDouble(), 4.375, 3000);
    QCOMPARE(trimBar->property("viewEndSec").toDouble(), 10.625);
    QCOMPARE(trimBar->property("zoomed").toBool(), true);
    QCOMPARE(backend.thumbRequestCount, 2);

    // Untouched since the last zoom, so Z now zooms back out to the whole video.
    QTest::keyClick(window, Qt::Key_Z);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("zoomed").toBool(), false, 3000);
    QCOMPARE(backend.thumbRequestCount, 3);
    QCOMPARE(backend.lastThumbStart, 0.0);
    QCOMPARE(backend.lastThumbEnd, 20.0);
}

void BackendTests::qmlQuitConfirmsUnexportedTrim() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    backend.announceInfo();
    QQuickItem *trimBar = harness.trimBar();
    QVERIFY(trimBar);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    // Trim the video, making the work unexported: Q now asks instead of quitting.
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("startSec").toDouble(), 5.0, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), true, 3000);

    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);

    // Escape backs out of the confirmation.
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), false, 3000);

    // The dialog is keyboard-driven: arrows move between the buttons instead
    // of seeking, and Enter presses the focused one. Right from the default
    // Export focus wraps around to Cancel, which closes without exporting.
    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Right);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), false, 3000);
    QCOMPARE(backend.exportCount, 0);
    QCOMPARE(trimBar->property("playheadSec").toDouble(), 5.0);

    // Enter on the default Export focus exports, as does Ctrl+S.
    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 1, 3000);
    QCOMPARE(window->property("quitConfirmVisible").toBool(), false);

    // The buttons work with the mouse too: Cancel dismisses, Export exports.
    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);
    QQuickItem *cancelButton = dialogButton(window, QStringLiteral("Cancel"));
    QVERIFY(cancelButton);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, itemCenter(cancelButton));
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), false, 3000);
    QCOMPARE(backend.exportCount, 1);

    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);
    QQuickItem *exportButton = dialogButton(window, QStringLiteral("Export"));
    QVERIFY(exportButton);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, itemCenter(exportButton));
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 2, 3000);
    QCOMPARE(window->property("quitConfirmVisible").toBool(), false);

    // A completed export cleans the trim; changing it again re-dirties.
    backend.announceExportDone();
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), false, 3000);
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Space, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("endSec").toDouble(), 10.0, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), true, 3000);

    // Confirming the quit really ends the app: the window closes instead of
    // being re-intercepted by onClosing, and Qt.quit() is requested too.
    QSignalSpy quitSpy(&harness.engine(), &QQmlApplicationEngine::quit);
    QTest::keyClick(window, Qt::Key_Q);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("quitConfirmVisible").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Left);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 3000);
    QCOMPARE(quitSpy.count(), 1);
}

QString BackendTests::makeSolidVideo(const QString &name, const QString &size) const {
    const QString path = m_dir.filePath(name);
    QProcess proc;
    proc.start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("color=c=black:size=%1:rate=10:duration=1").arg(size),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        path,
    });
    proc.waitForFinished(30000);
    return path;
}

void BackendTests::captionAssCarriesTheStyleAndRebasesTheClip() {
    const QList<subtitles::Cue> cues = {
        {0.5, 1.5, QStringLiteral("before the clip")},
        {3.0, 5.0, QStringLiteral("inside")},
        {1.0, 7.0, QStringLiteral("spans the whole clip")},
        {4.0, 4.5, QStringLiteral("   ")},  // never typed into: not a caption
        {9.0, 9.5, QStringLiteral("after the clip")},
    };

    subtitles::Style style;
    style.fontFamily = QStringLiteral("Test Sans");
    style.fontSize = 48;
    style.bold = true;
    style.textColor = QStringLiteral("#ff0000");
    style.outlineColor = QStringLiteral("#0000ff");
    style.outlineWidth = 3;
    style.marginV = 60;

    const QString ass = subtitles::buildAss(cues, style, 1080, 1920, 2.0, 6.0);

    // PlayRes is the video's own size, so the sizes above are in its pixels.
    QVERIFY(ass.contains(QStringLiteral("PlayResX: 1080")));
    QVERIFY(ass.contains(QStringLiteral("PlayResY: 1920")));

    // ASS colours are &HAABBGGRR, so red reads as 0000FF and blue as FF0000.
    QVERIFY2(ass.contains(QStringLiteral("Style: omacut,Test Sans,48,&H000000FF,&H000000FF,"
                                         "&H00FF0000,&H00FF0000,-1,0,0,0,100,100,0,0,"
                                         "1,3.0,0,2,65,65,60,1")),
             qPrintable(ass));

    // Only what shows up in [2, 6] survives, rebased so the clip starts at zero
    // and clamped to its bounds.
    QCOMPARE(ass.count(QStringLiteral("Dialogue:")), 2);
    QVERIFY(ass.contains(QStringLiteral("Dialogue: 0,0:00:01.00,0:00:03.00,omacut,,0,0,0,,inside")));
    QVERIFY(ass.contains(QStringLiteral("Dialogue: 0,0:00:00.00,0:00:04.00,omacut,,0,0,0,,"
                                        "spans the whole clip")));

    // The box replaces the outline, and its opacity inverts into ASS's alpha.
    style.box = true;
    style.boxColor = QStringLiteral("#000000");
    style.boxOpacity = 60;
    const QString boxed = subtitles::buildAss(cues, style, 1080, 1920, 2.0, 6.0);
    QVERIFY2(boxed.contains(QStringLiteral(",&H66000000,&H66000000,-1,0,0,0,100,100,0,0,3,")),
             qPrintable(boxed));

    // Timestamps round to centiseconds without rolling over into 60 seconds.
    QCOMPARE(subtitles::assTime(59.999), QStringLiteral("0:01:00.00"));
    QCOMPARE(subtitles::assTime(-1.0), QStringLiteral("0:00:00.00"));
    QCOMPARE(subtitles::assColor(QStringLiteral("#ffffff")), QStringLiteral("&H00FFFFFF"));
}

void BackendTests::captionAssEscapesTheAssMarkers() {
    const QList<subtitles::Cue> cues = {
        {0.0, 1.0, QStringLiteral("{an override} \\ and\ntwo lines")},
    };
    const QString ass = subtitles::buildAss(cues, subtitles::Style(), 640, 360, 0.0, 1.0);

    // Braces would start an override block and a bare newline would end the
    // event line, so both are escaped into something libass draws as typed.
    QVERIFY2(ass.contains(QStringLiteral(",,\\{an override\\} \\\\ and\\Ntwo lines")),
             qPrintable(ass));
    QCOMPARE(ass.count(QStringLiteral("Dialogue:")), 1);
}

void BackendTests::exportClipBurnsCaptionsIn() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    const QString source = makeSolidVideo(QStringLiteral("caption-source.mp4"),
                                          QStringLiteral("160x120"));
    QVERIFY(ffmpeg::probe(source).ok);
    QVERIFY(backend.load(QUrl::fromLocalFile(source)));
    waitForBackgroundWork(backend);

    const QVariantMap style{
        {QStringLiteral("fontFamily"), QStringLiteral("Liberation Sans")},
        {QStringLiteral("fontSize"), 24},
        {QStringLiteral("bold"), true},
        {QStringLiteral("textColor"), QStringLiteral("#ffffff")},
        {QStringLiteral("outlineColor"), QStringLiteral("#000000")},
        {QStringLiteral("outlineWidth"), 2},
        {QStringLiteral("marginV"), 10},
    };
    const auto cue = [](double start, double end, const QString &text) {
        return QVariant(QVariantMap{{QStringLiteral("start"), start},
                                    {QStringLiteral("end"), end},
                                    {QStringLiteral("text"), text}});
    };

    auto exportTo = [&](const QString &name) {
        QSignalSpy doneSpy(&backend, &Backend::exportDone);
        QSignalSpy failedSpy(&backend, &Backend::exportFailed);
        backend.exportClip(QUrl::fromLocalFile(m_dir.filePath(name)), 0.0, 1.0);
        QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() + failedSpy.count() > 0, 30000);
        QVERIFY2(failedSpy.count() == 0,
                 failedSpy.isEmpty() ? "" : qPrintable(failedSpy.first().at(0).toString()));
    };

    backend.setCaptions({}, style);
    QVERIFY(!backend.hasCaptionsIn(0.0, 1.0));
    exportTo(QStringLiteral("no-captions.mp4"));

    // Captions outside the exported range, and ones never typed into, are not
    // captions as far as the export is concerned.
    backend.setCaptions({cue(5.0, 6.0, QStringLiteral("later"))}, style);
    QVERIFY(!backend.hasCaptionsIn(0.0, 1.0));
    backend.setCaptions({cue(0.0, 1.0, QStringLiteral("  "))}, style);
    QVERIFY(!backend.hasCaptionsIn(0.0, 1.0));

    backend.setCaptions({cue(0.0, 1.0, QStringLiteral("burned in"))}, style);
    QVERIFY(backend.hasCaptionsIn(0.0, 1.0));
    exportTo(QStringLiteral("with-captions.mp4"));

    const QString plain = m_dir.filePath(QStringLiteral("no-captions.mp4"));
    const QString burned = m_dir.filePath(QStringLiteral("with-captions.mp4"));
    QVERIFY(ffmpeg::probe(burned).ok);
    // White text over a black frame is a lot of new detail; an export that
    // didn't burn anything in would be the same size as the plain one.
    QVERIFY2(QFileInfo(burned).size() > QFileInfo(plain).size(),
             qPrintable(QStringLiteral("plain %1 vs burned %2")
                            .arg(QFileInfo(plain).size())
                            .arg(QFileInfo(burned).size())));

    // The .ass is a temp file that only lives as long as the encode.
    const QStringList leftovers = QDir(QDir::tempPath())
        .entryList({QStringLiteral("omacut-captions-*.ass")}, QDir::Files);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QStringLiteral(", "))));
}

void BackendTests::srtSidecarNumbersAndRebasesTheCues() {
    const QList<subtitles::Cue> cues = {
        {0.5, 1.5, QStringLiteral("before the clip")},
        {3.0, 4.25, QStringLiteral("first")},
        {4.5, 4.75, QStringLiteral("  ")},  // never typed into: not a caption
        {5.0, 9.0, QStringLiteral("second, clamped to the end")},
        {9.5, 10.0, QStringLiteral("after the clip")},
    };

    // Same window as the burned-in captions get: rebased onto the clip, clamped
    // to it, and numbered from one in playback order.
    const QString srt = subtitles::buildSrt(cues, 2.0, 6.0);
    QCOMPARE(srt, QStringLiteral("1\n00:00:01,000 --> 00:00:02,250\nfirst\n\n"
                                 "2\n00:00:03,000 --> 00:00:04,000\n"
                                 "second, clamped to the end\n\n"));

    // Nothing in the clip means no file at all, not an empty one.
    QVERIFY(subtitles::buildSrt(cues, 20.0, 30.0).isEmpty());
    QVERIFY(subtitles::buildSrt({}, 0.0, 10.0).isEmpty());

    // SubRip counts milliseconds after a comma, and pads the hours.
    QCOMPARE(subtitles::srtTime(3661.5), QStringLiteral("01:01:01,500"));
    QCOMPARE(subtitles::srtTime(-1.0), QStringLiteral("00:00:00,000"));
}

void BackendTests::exportOffersTheSidecarOnlyWhenCaptionsAreInTheClip() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const auto cue = [](double start, double end, const QString &text) {
        return QVariant(QVariantMap{{QStringLiteral("start"), start},
                                    {QStringLiteral("end"), end},
                                    {QStringLiteral("text"), text}});
    };

    // No captions: the save dialog has nothing to ask about.
    backend.exportDialog(0.0, 1.0);
    QCOMPARE(picker->exportCount, 1);
    QCOMPARE(picker->lastOfferedSidecar, false);

    // Captions, but not in the exported stretch: still nothing to offer.
    backend.setCaptions({cue(5.0, 6.0, QStringLiteral("later"))}, {});
    backend.exportDialog(0.0, 1.0);
    QCOMPARE(picker->lastOfferedSidecar, false);

    backend.setCaptions({cue(0.2, 0.8, QStringLiteral("in shot"))}, {});
    backend.exportDialog(0.0, 1.0);
    QCOMPARE(picker->lastOfferedSidecar, true);
}

void BackendTests::exportClipWritesTheSrtSidecar() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const QVariantMap style{{QStringLiteral("fontFamily"), QStringLiteral("Liberation Sans")},
                            {QStringLiteral("fontSize"), 24}};
    backend.setCaptions({QVariant(QVariantMap{{QStringLiteral("start"), 0.2},
                                              {QStringLiteral("end"), 0.8},
                                              {QStringLiteral("text"), QStringLiteral("in shot")}})},
                        style);

    // The test macros can't live in a value-returning lambda, so the sidecar
    // path exportDone reported comes back out here.
    QString reportedSidecar;
    const auto exportTo = [&](const QString &name, bool sidecar) {
        QSignalSpy doneSpy(&backend, &Backend::exportDone);
        QSignalSpy failedSpy(&backend, &Backend::exportFailed);
        reportedSidecar.clear();
        backend.exportClip(QUrl::fromLocalFile(m_dir.filePath(name)), 0.0, 1.0, 0, sidecar);
        QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() + failedSpy.count() > 0, 30000);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(doneSpy.count(), 1);
        reportedSidecar = doneSpy.first().at(1).toString();
    };

    // Asked for: the .srt lands next to the video, named after it, holding the
    // captions rebased onto the exported clip.
    exportTo(QStringLiteral("with-srt.mp4"), true);
    const QString srtPath = reportedSidecar;
    QCOMPARE(srtPath, m_dir.filePath(QStringLiteral("with-srt.srt")));
    QFile srt(srtPath);
    QVERIFY(srt.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(srt.readAll()),
             QStringLiteral("1\n00:00:00,200 --> 00:00:00,800\nin shot\n\n"));
    srt.close();

    // Not asked for: only the video is written, and exportDone says as much.
    exportTo(QStringLiteral("no-srt.mp4"), false);
    QCOMPARE(reportedSidecar, QString());
    QVERIFY(!QFileInfo::exists(m_dir.filePath(QStringLiteral("no-srt.srt"))));

    // A failed export leaves an .srt that was already sitting there alone.
    const QString keptPath = m_dir.filePath(QStringLiteral("keep-srt.srt"));
    const QByteArray original("someone else's subtitles");
    {
        QFile existing(keptPath);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        existing.write(original);
    }

    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    QVERIFY(installBrokenFfmpeg(pathDir.path()));
    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()) + ':' + qgetenv("PATH"));

    QSignalSpy failedSpy(&backend, &Backend::exportFailed);
    backend.exportClip(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("keep-srt.mp4"))),
                       0.0, 1.0, 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);

    QFile kept(keptPath);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), original);
}

void BackendTests::qmlCaptionEditorAddsRetimesAndTypes() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    backend.announceInfo();
    QQuickItem *trimBar = harness.trimBar();
    QVERIFY(trimBar);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    // C opens the subtitle editor.
    QTest::keyClick(window, Qt::Key_C);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("captionMode").toBool(), true, 3000);

    // T drops a two-second caption at the playhead and selects it.
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 5.0, 3000);
    QTest::keyClick(window, Qt::Key_T);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("selectedCaption").toInt(), 0, 3000);
    QCOMPARE(backend.lastCues.size(), 1);
    QCOMPARE(backend.lastCues.first().toMap().value(QStringLiteral("start")).toDouble(), 5.0);
    QCOMPARE(backend.lastCues.first().toMap().value(QStringLiteral("end")).toDouble(), 7.0);

    // The new caption takes the keyboard, so the shortcuts stop swallowing keys:
    // this types a caption instead of zooming, playing and quitting.
    QTRY_VERIFY_WITH_TIMEOUT(window->property("typing").toBool(), 3000);
    const QString typed = QStringLiteral("zoom q me");
    for (const QChar ch : typed)
        QTest::keyClick(window, ch.toLatin1());
    QTRY_COMPARE_WITH_TIMEOUT(backend.lastCues.first().toMap().value(QStringLiteral("text")).toString(),
                              typed, 3000);
    QCOMPARE(trimBar->property("zoomed").toBool(), false);
    QCOMPARE(window->property("quitConfirmVisible").toBool(), false);
    QCOMPARE(window->property("activeCaptionText").toString(), typed);

    // Escape hands the keyboard back, and Z zooms again.
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("typing").toBool(), false, 3000);
    QTest::keyClick(window, Qt::Key_Z);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("zoomed").toBool(), true, 3000);

    // ] pulls the caption's end to the playhead, [ its start.
    QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 6.0, 3000);
    QTest::keyClick(window, Qt::Key_BracketRight);
    QTRY_COMPARE_WITH_TIMEOUT(backend.lastCues.first().toMap().value(QStringLiteral("end")).toDouble(),
                              6.0, 3000);
    QTest::keyClick(window, Qt::Key_Left);
    QTRY_COMPARE_WITH_TIMEOUT(trimBar->property("playheadSec").toDouble(), 5.0, 3000);
    QTest::keyClick(window, Qt::Key_BracketLeft);
    QTRY_COMPARE_WITH_TIMEOUT(backend.lastCues.first().toMap().value(QStringLiteral("start")).toDouble(),
                              5.0, 3000);

    // Dragging the block's middle slides the whole caption along the lane.
    QQuickItem *captionTrack = window->findChild<QQuickItem *>(QStringLiteral("captionTrack"));
    QVERIFY(captionTrack);
    const double windowStart = captionTrack->property("windowStart").toDouble();
    const double windowLen = captionTrack->property("windowEnd").toDouble() - windowStart;
    const double trackX = captionTrack->property("trackX").toDouble();
    const double trackW = captionTrack->property("trackW").toDouble();
    QVERIFY(windowLen > 0 && trackW > 0);
    const auto pointAt = [&](double seconds) {
        return captionTrack
            ->mapToScene(QPointF(trackX + (seconds - windowStart) / windowLen * trackW,
                                 captionTrack->height() / 2))
            .toPoint();
    };
    const auto cueStart = [&backend] {
        return backend.lastCues.first().toMap().value(QStringLiteral("start")).toDouble();
    };

    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pointAt(5.5));
    QTest::mouseMove(window, pointAt(6.5));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, pointAt(6.5));
    // A pixel of slack: the drag lands on whole pixels, not whole seconds.
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(cueStart() - 6.0) < 0.1, 3000);
    QVERIFY(qAbs(backend.lastCues.first().toMap().value(QStringLiteral("end")).toDouble() - 7.0) < 0.1);

    // Delete removes it, and the backend is told the list is empty again.
    QTest::keyClick(window, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("selectedCaption").toInt(), -1, 3000);
    QCOMPARE(backend.lastCues.size(), 0);
}

void BackendTests::qmlCaptionsCountAsUnexportedWork() {
    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            20.0);
    QmlHarness harness(backend);

    QVERIFY2(harness.window(), qPrintable(mainQmlPath()));
    QQuickWindow *window = harness.window();
    QTRY_VERIFY_WITH_TIMEOUT(window->property("audioOutputReady").toBool(), 3000);

    backend.announceInfo();
    window->show();
    window->requestActivate();
    QTest::qWait(100);

    // An untouched video is clean even though the trim spans all of it.
    QCOMPARE(window->property("trimDirty").toBool(), false);

    // A caption is unexported work, exactly like a trim.
    QTest::keyClick(window, Qt::Key_T);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("typing").toBool(), false, 3000);

    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 1, 3000);
    backend.announceExportDone();
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), false, 3000);

    // Another caption after that export is unexported work again.
    QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_T);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), true, 3000);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("typing").toBool(), false, 3000);

    // Deleting them all leaves nothing worth exporting, just like an untouched
    // trim, so the prompt stays out of the way.
    QTest::keyClick(window, Qt::Key_Delete);
    QCOMPARE(window->property("trimDirty").toBool(), true);
    QTest::keyClick(window, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(window->property("hasCaptions").toBool(), false, 3000);
    QCOMPARE(window->property("trimDirty").toBool(), false);

    // Opening another video starts from a clean slate.
    backend.announceInfo();
    QTRY_COMPARE_WITH_TIMEOUT(window->property("trimDirty").toBool(), false, 3000);
    QCOMPARE(window->property("hasCaptions").toBool(), false);
}

void BackendTests::trimArgsReencodeForPreciseCuts() {
    const QStringList args = ffmpeg::trimArgs(QStringLiteral("in.mp4"),
                                              QStringLiteral("out.mp4"),
                                              0.25, 0.75);

    QVERIFY(args.contains(QStringLiteral("libx264")));
    QVERIFY(args.contains(QStringLiteral("aac")));
    QVERIFY(args.contains(QStringLiteral("+faststart")));
    QVERIFY(!args.contains(QStringLiteral("copy")));

    // Progress reporting goes to stdout so the UI can show a percentage.
    const int progressAt = args.indexOf(QStringLiteral("-progress"));
    QVERIFY(progressAt >= 0);
    QCOMPARE(args.value(progressAt + 1), QStringLiteral("pipe:1"));
}

void BackendTests::trimArgsScaleTheShorterSide() {
    // No scale request, no scale filter.
    QVERIFY(!ffmpeg::trimArgs(QStringLiteral("in.mp4"), QStringLiteral("out.mp4"), 0.0, 1.0)
                 .contains(QStringLiteral("-vf")));

    // The filter caps whichever side is shorter, keeping the aspect ratio for
    // portrait and landscape alike.
    const QStringList args = ffmpeg::trimArgs(QStringLiteral("in.mp4"), QStringLiteral("out.mp4"),
                                              0.0, 1.0, 1080);
    const int vfAt = args.indexOf(QStringLiteral("-vf"));
    QVERIFY(vfAt >= 0);
    QCOMPARE(args.value(vfAt + 1),
             QStringLiteral("scale='if(gt(iw,ih),-2,1080)':'if(gt(iw,ih),1080,-2)'"));
}

void BackendTests::trimArgsBurnCaptionsAfterTheDownscale() {
    const QStringList args = ffmpeg::trimArgs(QStringLiteral("in.mp4"), QStringLiteral("out.mp4"),
                                              0.0, 1.0, 720,
                                              QStringLiteral("/tmp/odd dir/caps:1.ass"));
    const int filterAt = args.indexOf(QStringLiteral("-vf"));
    QVERIFY(filterAt >= 0);
    const QString filters = args.value(filterAt + 1);

    // The downscale runs first, so libass draws the text at the export's own
    // resolution instead of it being resampled along with the frame.
    QVERIFY2(filters.startsWith(QStringLiteral("scale=")), qPrintable(filters));
    QVERIFY2(filters.indexOf(QStringLiteral("scale=")) < filters.indexOf(QStringLiteral("subtitles=")),
             qPrintable(filters));
    // A colon in the path would otherwise split the filter's arguments.
    QVERIFY2(filters.endsWith(QStringLiteral(",subtitles=filename=/tmp/odd dir/caps\\:1.ass")),
             qPrintable(filters));

    // No downscale: the subtitle filter stands alone.
    const QStringList plain = ffmpeg::trimArgs(QStringLiteral("in.mp4"), QStringLiteral("out.mp4"),
                                               0.0, 1.0, 0, QStringLiteral("/tmp/caps.ass"));
    QCOMPARE(plain.value(plain.indexOf(QStringLiteral("-vf")) + 1),
             QStringLiteral("subtitles=filename=/tmp/caps.ass"));

    // And no captions at all leaves the argument list exactly as it was.
    QVERIFY(!ffmpeg::trimArgs(QStringLiteral("in.mp4"), QStringLiteral("out.mp4"), 0.0, 1.0)
                 .contains(QStringLiteral("-vf")));
}

void BackendTests::exportHeightsNeverUpscale() {
    QCOMPARE(Backend::exportHeights(3840, 2160), (QList<int>{1080, 720}));
    // Portrait sources are judged by their shorter side too.
    QCOMPARE(Backend::exportHeights(2160, 3840), (QList<int>{1080, 720}));
    QCOMPARE(Backend::exportHeights(1920, 1080), (QList<int>{720}));
    // At or below a target there's nothing to gain, so it isn't offered.
    QCOMPARE(Backend::exportHeights(1280, 720), QList<int>{});
    QCOMPARE(Backend::exportHeights(0, 0), QList<int>{});
}

void BackendTests::themeAccentReadsOmarchyColors() {
    const QString fallback = QStringLiteral("#FFD60A");
    const QString colorsPath = m_dir.filePath(QStringLiteral("colors.toml"));

    // No file at all — the non-omarchy case — keeps the fallback.
    QCOMPARE(Backend::accentFromColorsFile(m_dir.filePath(QStringLiteral("missing.toml")), fallback),
             fallback);

    const auto writeColors = [&colorsPath](const char *contents) {
        QFile file(colorsPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(contents);
    };

    writeColors("# omarchy theme\n"
                "mode = \"dark\"\n"
                "background = \"#121212\"\n"
                "accent = \"#33ccff\"\n");
    QCOMPARE(Backend::accentFromColorsFile(colorsPath, fallback), QStringLiteral("#33ccff"));

    writeColors("accent = '#aabbcc'\n");
    QCOMPARE(Backend::accentFromColorsFile(colorsPath, fallback), QStringLiteral("#aabbcc"));

    // A value that isn't a color keeps the fallback rather than breaking bindings.
    writeColors("accent = \"not-a-color\"\n");
    QCOMPARE(Backend::accentFromColorsFile(colorsPath, fallback), fallback);

    // As does a theme without an accent at all.
    writeColors("background = \"#121212\"\n");
    QCOMPARE(Backend::accentFromColorsFile(colorsPath, fallback), fallback);
}

void BackendTests::themeAccentForegroundKeepsContrast() {
    QCOMPARE(Backend::foregroundFor(QStringLiteral("#FFD60A")), QStringLiteral("black"));
    QCOMPARE(Backend::foregroundFor(QStringLiteral("#222266")), QStringLiteral("white"));
    QCOMPARE(Backend::foregroundFor(QStringLiteral("garbage")), QStringLiteral("black"));
}

QTEST_MAIN(BackendTests)
#include "backend_tests.moc"
