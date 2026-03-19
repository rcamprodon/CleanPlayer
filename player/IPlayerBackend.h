#pragma once

#include <QObject>
#include <QRect>
#include <QStringList>
#include <QUrl>

struct PlaylistItem {
    QUrl url;
    QString displayName;
    qint64 durationMs = -1;
    double fps = 0.0;
    int width = 0;
    int height = 0;
    double aspectRatio = 0.0;
};

class IPlayerBackend : public QObject
{
    Q_OBJECT
public:
    enum class PlaybackState { Stopped, Playing, Paused };
    enum class Status { NoMedia, Loading, Loaded, Buffering, Ended, Error };

    explicit IPlayerBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IPlayerBackend() override = default;

    virtual void setVideoContainer(QWidget *container) = 0;

    // Playlist
    virtual int playlistSize() const = 0;
    virtual QList<PlaylistItem> playlist() const = 0;

    virtual void clearPlaylist() = 0;
    virtual void addToPlaylist(const QUrl &url) = 0;
    virtual void removeFromPlaylist(int index) = 0;
    virtual void movePlaylistItem(int from, int to) = 0;  // <-- NO override here

    virtual int currentIndex() const = 0;
    virtual void setCurrentIndex(int index) = 0;

    // Transport
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;

    virtual bool loopEnabled() const = 0;
    virtual void setLoopEnabled(bool enabled) = 0;

    // Time
    virtual qint64 positionMs() const = 0;
    virtual void setPositionMs(qint64 ms) = 0;
    virtual qint64 durationMs() const = 0;

    // Audio
    virtual int volume100() const = 0;
    virtual void setVolume100(int v) = 0;

    // External display selection
    virtual int screenCount() const = 0;
    virtual QRect screenGeometry(int screenIndex) const = 0;

    virtual int outputScreen() const = 0;
    virtual void setOutputScreen(int screenIndex) = 0;

    // Optional capabilities (VLC implements; Qt backend may stub)
    virtual QStringList audioOutputs() const { return {}; }
    virtual bool setAudioOutput(int) { return false; }
    virtual int currentAudioOutput() const { return -1; }

    virtual QStringList subtitleTracks() const { return {}; }
    virtual bool setSubtitleTrack(int) { return false; } // -1 == Off at UI level
    virtual int currentSubtitleTrack() const { return -1; }

signals:
    void playlistChanged();
    void currentIndexChanged(int index);

    void playbackStateChanged(IPlayerBackend::PlaybackState);
    void statusChanged(IPlayerBackend::Status);

    void positionChanged(qint64 posMs);
    void durationChanged(qint64 durMs);

    void outputScreenChanged(int screenIndex);

    void errorOccurred(const QString &message);

    // Diagnostics
    void fpsDetected(double fps, const QString &method);

    // NEW
    void framesDropped(int totalDrops, int displayedFrames);
    void subtitlesChanged();
};