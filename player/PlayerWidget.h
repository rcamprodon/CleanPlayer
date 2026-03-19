#pragma once

#include <QWidget>

class IPlayerBackend;

class QPushButton;
class QLabel;
class QListWidget;
class QComboBox;
class QCheckBox;
class QSlider;

class PlayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerWidget(IPlayerBackend *backend, QWidget *parent = nullptr);

private:
    void refreshPlaylistUI();
    void updateTimeLabel(qint64 posMs);
    void rebuildScreensCombo();
    void refreshSubtitlesUI();                 // NEW

private:
    IPlayerBackend *m_backend = nullptr;

    QWidget *m_videoContainer = nullptr;

    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;

    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;

    QCheckBox *m_loopCheck = nullptr;

    QSlider *m_volumeSlider = nullptr;
    QSlider *m_seekSlider = nullptr;
    bool m_userSeeking = false;
    qint64 m_lastDurationMs = 0;

    // NEW: subtitles + dropped frames UI
    QComboBox *m_subtitlesCombo = nullptr;
    bool m_updatingSubtitlesCombo = false;
    QLabel *m_dropsLabel = nullptr;

    QLabel *m_timeLabel = nullptr;

    QListWidget *m_playlistView = nullptr;
    QComboBox *m_outputScreenCombo = nullptr;
    QLabel *m_fpsLabel = nullptr;
};