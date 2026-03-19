#include "PlayerWidget.h"
#include "IPlayerBackend.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QUrl>

#include <algorithm>

static QString formatHHMMSS(qint64 ms) {
  if (ms < 0)
    ms = 0;
  const qint64 s = ms / 1000;
  const int hh = int(s / 3600);
  const int mm = int((s % 3600) / 60);
  const int ss = int(s % 60);
  return QString("%1:%2:%3")
      .arg(hh, 2, 10, QChar('0'))
      .arg(mm, 2, 10, QChar('0'))
      .arg(ss, 2, 10, QChar('0'));
}

PlayerWidget::PlayerWidget(IPlayerBackend *backend, QWidget *parent)
    : QWidget(parent), m_backend(backend) {
  Q_ASSERT(m_backend);

  auto *root = new QVBoxLayout(this);

  // Video container with its own layout so backend can just parent its render
  // widget into it
  m_videoContainer = new QWidget(this);
  m_videoContainer->setMinimumSize(640, 360);
  m_videoContainer->setStyleSheet("background:black;");
  m_videoContainer->setLayout(new QVBoxLayout());
  m_videoContainer->layout()->setContentsMargins(0, 0, 0, 0);
  root->addWidget(m_videoContainer, 1);

  auto *row0 = new QHBoxLayout();
  m_seekSlider = new QSlider(Qt::Horizontal, this);
  m_seekSlider->setRange(0, 1000); // normalized 0..1000
  m_seekSlider->setValue(0);
  m_seekSlider->setMinimumWidth(260);
  m_seekSlider->setEnabled(false);
  row0->addWidget(m_seekSlider);
  root->addLayout(row0);

  auto *row1 = new QHBoxLayout();
  m_loadBtn = new QPushButton("Load", this);
  m_addBtn = new QPushButton("Add", this);
  m_removeBtn = new QPushButton("Remove", this);
  m_upBtn = new QPushButton("Up", this);
  m_downBtn = new QPushButton("Down", this);
  row1->addWidget(m_loadBtn);
  row1->addWidget(m_addBtn);
  row1->addWidget(m_removeBtn);
  row1->addWidget(m_upBtn);
  row1->addWidget(m_downBtn);
  row1->addStretch(1);
  root->addLayout(row1);

  auto *row2 = new QHBoxLayout();
  m_prevBtn = new QPushButton("Prev", this);
  m_playBtn = new QPushButton("Play", this);
  m_pauseBtn = new QPushButton("Pause", this);
  m_stopBtn = new QPushButton("Stop", this);
  m_nextBtn = new QPushButton("Next", this);
  m_loopCheck = new QCheckBox("Loop", this);
  row2->addWidget(m_prevBtn);
  row2->addWidget(m_playBtn);
  row2->addWidget(m_pauseBtn);
  row2->addWidget(m_stopBtn);
  row2->addWidget(m_nextBtn);
  row2->addWidget(m_loopCheck);
  row2->addStretch(1);
  root->addLayout(row2);

  auto *row3 = new QHBoxLayout();
  m_outputScreenCombo = new QComboBox(this);
  m_volumeSlider = new QSlider(Qt::Horizontal, this);
  m_volumeSlider->setRange(0, 100);
  m_volumeSlider->setValue(m_backend->volume100());
  m_volumeSlider->setFixedWidth(140);
  row3->addWidget(m_loopCheck);
  row3->addWidget(new QLabel("Output:", this));
  row3->addWidget(m_outputScreenCombo);
  row3->addSpacing(10);
  row3->addWidget(new QLabel("Vol:", this));
  row3->addWidget(m_volumeSlider);
  row3->addStretch(1);
  root->addLayout(row3);

  auto *row4 = new QHBoxLayout();
  m_subtitlesCombo = new QComboBox(this);
  m_subtitlesCombo->setMinimumWidth(180);
  m_fpsLabel = new QLabel("fps: -", this);
  m_dropsLabel = new QLabel("drops: -", this);
  m_timeLabel = new QLabel("00:00:00", this);
  row4->addWidget(new QLabel("Subs:", this));
  row4->addWidget(m_subtitlesCombo);
  row4->addSpacing(10);
  row4->addWidget(m_fpsLabel);
  row4->addWidget(m_dropsLabel);
  row4->addWidget(m_timeLabel);
  row4->addStretch(1);
  root->addLayout(row4);

  auto row5 = new QHBoxLayout();
  m_playlistView = new QListWidget(this);
  row5->addWidget(m_playlistView);
  root->addLayout(row5);

  // Connect backend -> UI
  m_backend->setVideoContainer(m_videoContainer);

  connect(m_backend, &IPlayerBackend::playlistChanged, this, [this] {
    refreshPlaylistUI();
    refreshSubtitlesUI(); // NEW: current item may have changed media
  });
  connect(m_backend, &IPlayerBackend::currentIndexChanged, this,
          [this](int idx) {
            m_playlistView->setCurrentRow(idx);
            refreshSubtitlesUI(); // NEW
          });

  connect(m_backend, &IPlayerBackend::subtitlesChanged, this,
          [this] { refreshSubtitlesUI(); });

  connect(m_backend, &IPlayerBackend::durationChanged, this,
          [this](qint64 durMs) {
            m_lastDurationMs = durMs;
            if (durMs <= 0) {
              m_seekSlider->setEnabled(false);
              m_seekSlider->setValue(0);
            } else {
              m_seekSlider->setEnabled(true);
            }
          });

  connect(m_backend, &IPlayerBackend::positionChanged, this,
          [this](qint64 posMs) {
            updateTimeLabel(posMs);

            if (!m_userSeeking && m_lastDurationMs > 0) {
              const int v = int((posMs * 1000) / m_lastDurationMs);
              m_seekSlider->setValue(std::clamp(v, 0, 1000));
            }
          });

  connect(m_backend, &IPlayerBackend::fpsDetected, this,
          [this](double fps, const QString &method) {
            m_fpsLabel->setText(QString("fps: %1").arg(fps, 0, 'f', 3));
            m_fpsLabel->setToolTip(method);
          });

  // NEW: dropped frames monitor
  connect(m_backend, &IPlayerBackend::framesDropped, this,
          [this](int drops, int displayed) {
            m_dropsLabel->setText(
                QString("drops: %1/%2").arg(drops).arg(displayed));
          });

  // Screen combo
  rebuildScreensCombo();
  connect(m_backend, &IPlayerBackend::outputScreenChanged, this, [this](int s) {
    for (int i = 0; i < m_outputScreenCombo->count(); ++i) {
      if (m_outputScreenCombo->itemData(i).toInt() == s) {
        m_outputScreenCombo->setCurrentIndex(i);
        break;
      }
    }
  });

  // UI events
  connect(m_playBtn, &QPushButton::clicked, m_backend, &IPlayerBackend::play);
  connect(m_pauseBtn, &QPushButton::clicked, m_backend, &IPlayerBackend::pause);
  connect(m_stopBtn, &QPushButton::clicked, m_backend, &IPlayerBackend::stop);
  connect(m_prevBtn, &QPushButton::clicked, m_backend,
          &IPlayerBackend::previous);
  connect(m_nextBtn, &QPushButton::clicked, m_backend, &IPlayerBackend::next);

  connect(m_loopCheck, &QCheckBox::toggled, this,
          [this](bool on) { m_backend->setLoopEnabled(on); });

  connect(m_outputScreenCombo, &QComboBox::currentIndexChanged, this,
          [this](int idx) {
            const int screenIndex = m_outputScreenCombo->itemData(idx).toInt();
            m_backend->setOutputScreen(screenIndex);
          });

  connect(m_volumeSlider, &QSlider::valueChanged, this,
          [this](int v) { m_backend->setVolume100(v); });

  // Seek handling
  connect(m_seekSlider, &QSlider::sliderPressed, this,
          [this] { m_userSeeking = true; });

  connect(m_seekSlider, &QSlider::sliderReleased, this, [this] {
    m_userSeeking = false;
    if (m_lastDurationMs > 0) {
      const int v = m_seekSlider->value();
      const qint64 newPos = (m_lastDurationMs * v) / 1000;
      m_backend->setPositionMs(newPos);
    }
  });

  // Optional continuous seeking while dragging
  connect(m_seekSlider, &QSlider::valueChanged, this, [this](int v) {
    if (!m_userSeeking)
      return;
    if (m_lastDurationMs <= 0)
      return;
    const qint64 newPos = (m_lastDurationMs * v) / 1000;
    m_backend->setPositionMs(newPos);
  });

  // NEW: subtitle selection
  connect(m_subtitlesCombo, &QComboBox::currentIndexChanged, this,
          [this](int comboIndex) {
            if (m_updatingSubtitlesCombo)
              return;

            // comboIndex 0 = Off, 1..N = track index 0..N-1
            const int trackIndex = comboIndex - 1;
            m_backend->setSubtitleTrack(trackIndex);
          });

  // Playlist ops
  connect(m_loadBtn, &QPushButton::clicked, this, [this] {
    const QString file = QFileDialog::getOpenFileName(
        this, "Load video", QString(),
        "Video files (*.mkv *.mp4 *.avi *.mov *.webm);;All files (*)");
    if (file.isEmpty())
      return;

    m_backend->clearPlaylist();
    m_backend->addToPlaylist(QUrl::fromLocalFile(file));
    m_backend->setCurrentIndex(0);
    m_backend->play();
  });

  connect(m_addBtn, &QPushButton::clicked, this, [this] {
    const QString file = QFileDialog::getOpenFileName(
        this, "Add to playlist", QString(),
        "Video files (*.mkv *.mp4 *.avi *.mov *.webm);;All files (*)");
    if (file.isEmpty())
      return;
    m_backend->addToPlaylist(QUrl::fromLocalFile(file));
  });

  connect(m_removeBtn, &QPushButton::clicked, this, [this] {
    const int row = m_playlistView->currentRow();
    if (row >= 0)
      m_backend->removeFromPlaylist(row);
  });

  connect(m_upBtn, &QPushButton::clicked, this, [this] {
    const int row = m_playlistView->currentRow();
    if (row > 0) {
      m_backend->movePlaylistItem(row, row - 1);
      m_backend->setCurrentIndex(row - 1);
    }
  });

  connect(m_downBtn, &QPushButton::clicked, this, [this] {
    const int row = m_playlistView->currentRow();
    if (row >= 0 && row < m_backend->playlistSize() - 1) {
      m_backend->movePlaylistItem(row, row + 1);
      m_backend->setCurrentIndex(row + 1);
    }
  });

  connect(m_playlistView, &QListWidget::currentRowChanged, this,
          [this](int row) {
            if (row >= 0)
              m_backend->setCurrentIndex(row);
          });

  refreshPlaylistUI();
  refreshSubtitlesUI();
}

void PlayerWidget::rebuildScreensCombo() {
  m_outputScreenCombo->clear();
  const int screens = m_backend->screenCount();
  for (int i = 0; i < screens; ++i) {
    const QString label =
        (i == 0) ? "0 (Local)" : QString("%1 (External)").arg(i);
    m_outputScreenCombo->addItem(label, i);
  }

  const int out = m_backend->outputScreen();
  for (int i = 0; i < m_outputScreenCombo->count(); ++i) {
    if (m_outputScreenCombo->itemData(i).toInt() == out) {
      m_outputScreenCombo->setCurrentIndex(i);
      break;
    }
  }
}

void PlayerWidget::refreshPlaylistUI() {
  m_playlistView->clear();
  const auto items = m_backend->playlist();
  for (int i = 0; i < items.size(); ++i) {
    const QString name = !items[i].displayName.isEmpty()
                             ? items[i].displayName
                             : items[i].url.toString();

    const QString meta =
        (items[i].fps > 0.0)
            ? QString("  [%1 fps]").arg(items[i].fps, 0, 'f', 3)
            : QString();

    m_playlistView->addItem(QString("%1. %2%3").arg(i).arg(name).arg(meta));
  }
  m_playlistView->setCurrentRow(m_backend->currentIndex());
}

void PlayerWidget::refreshSubtitlesUI() {
  m_updatingSubtitlesCombo = true;

  m_subtitlesCombo->clear();
  m_subtitlesCombo->addItem("Off"); // index 0

  const QStringList tracks = m_backend->subtitleTracks();
  for (const auto &t : tracks)
    m_subtitlesCombo->addItem(t);

  // Map backend currentSubtitleTrack (-1=off, else 0..N-1) to combo index
  const int cur = m_backend->currentSubtitleTrack();
  const int comboIndex = cur + 1; // -1 -> 0, 0 -> 1 ...
  if (comboIndex >= 0 && comboIndex < m_subtitlesCombo->count())
    m_subtitlesCombo->setCurrentIndex(comboIndex);
  else
    m_subtitlesCombo->setCurrentIndex(0);

  // If no tracks, disable but keep "Off"
  m_subtitlesCombo->setEnabled(!tracks.isEmpty());

  m_updatingSubtitlesCombo = false;
}

void PlayerWidget::updateTimeLabel(qint64 posMs) {
  m_timeLabel->setText(formatHHMMSS(posMs));
}
