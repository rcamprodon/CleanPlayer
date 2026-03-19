#pragma once

#include "IPlayerBackend.h"

#include <QElapsedTimer>

class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QWidget;
class QVideoFrame;

class QtMediaBackend final : public IPlayerBackend
{
    Q_OBJECT
public:
    explicit QtMediaBackend(QObject *parent = nullptr);
    ~QtMediaBackend() override;

    void setVideoContainer(QWidget *container) override;

    int playlistSize() const override;
    QList<PlaylistItem> playlist() const override;

    void clearPlaylist() override;
    void addToPlaylist(const QUrl &url) override;
    void removeFromPlaylist(int index) override;
    void movePlaylistItem(int from, int to) override;

    int currentIndex() const override;
    void setCurrentIndex(int index) override;

    void play() override;
    void pause() override;
    void stop() override;
    void next() override;
    void previous() override;

    bool loopEnabled() const override;
    void setLoopEnabled(bool enabled) override;

    qint64 positionMs() const override;
    void setPositionMs(qint64 ms) override;
    qint64 durationMs() const override;

    int volume100() const override;
    void setVolume100(int v) override;

    int screenCount() const override;
    QRect screenGeometry(int screenIndex) const override;

    int outputScreen() const override;
    void setOutputScreen(int screenIndex) override;

    // Subtitles not supported (yet) in Qt backend; keep stubs consistent
    QStringList subtitleTracks() const override { return {}; }
    bool setSubtitleTrack(int) override { return false; }
    int currentSubtitleTrack() const override { return -1; }

private:
    void setSourceFromCurrentIndex(bool autoplay);
    void updateCurrentItemFromMetaData();
    void resetFpsDetection();
    void onVideoFrameArrived(const QVideoFrame &frame);
    void reportFpsIfNeeded(const char *method, double fps);

private:
    QList<PlaylistItem> m_playlist;
    int m_currentIndex = -1;
    bool m_loop = false;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;

    QWidget *m_container = nullptr;
    QVideoWidget *m_videoWidget = nullptr;

    QWidget *m_externalWindow = nullptr;
    int m_outputScreen = 0;

    // FPS detection
    double m_fpsMeta = 0.0;
    double m_fpsEstimated = 0.0;
    bool m_reportedMeta = false;
    bool m_reportedEstimated = false;

    QElapsedTimer m_fpsTimer;
    bool m_fpsTimerRunning = false;
    int m_framesInWindow = 0;
};