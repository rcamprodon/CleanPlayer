#pragma once

#include "IPlayerBackend.h"

#include <QTimer>
#include <QStringList>
#include <QVector>

#include <vlc/vlc.h>

class QWidget;

class VlcBackend final : public IPlayerBackend
{
    Q_OBJECT
public:
    explicit VlcBackend(QObject *parent = nullptr);
    ~VlcBackend() override;

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

    // Optional implemented (VLC)
    QStringList subtitleTracks() const override;
    bool setSubtitleTrack(int index) override;
    int currentSubtitleTrack() const override;

private:
    struct SubtitleCache {
        QStringList names;
        QVector<int> spuIds;
        int currentIndex = -1; // -1 = Off
        bool valid = false;
    };

    void bindVlcToWidget(QWidget *w);
    void setSourceFromCurrentIndex(bool autoplay);

    void installVlcEvents();
    void emitStateFromVlcState();

    void pollPositionDuration();
    void pollStats();

    void refreshSubtitlesForCurrentMedia();
    void applyCachedSubtitleToPlayer();

    static void handleVlcEvent(const libvlc_event_t *ev, void *ud);

private:
    QList<PlaylistItem> m_playlist;
    QVector<SubtitleCache> m_subCache;

    int m_currentIndex = -1;
    bool m_loop = false;

    QWidget *m_container = nullptr;
    QWidget *m_renderWidget = nullptr;

    QWidget *m_externalWindow = nullptr;
    int m_outputScreen = 0;

    libvlc_instance_t *m_vlc = nullptr;
    libvlc_media_player_t *m_mp = nullptr;

    QTimer m_pollTimer;
    qint64 m_lastDuration = -1;

    QTimer m_statsTimer;
    int m_lastLostPictures = -1;
    int m_lastDisplayedPictures = -1;
};