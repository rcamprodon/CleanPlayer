#include "QtMediaBackend.h"

#include <QAudioOutput>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLayout>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QWidget>

#include <algorithm>
#include <cmath>

static int clampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

QtMediaBackend::QtMediaBackend(QObject *parent) : IPlayerBackend(parent) {
  m_player = new QMediaPlayer(this);
  m_audio = new QAudioOutput(this);

  m_player->setAudioOutput(m_audio);
  m_audio->setVolume(0.8f);

  connect(m_player, &QMediaPlayer::positionChanged, this,
          [this](qint64 pos) { emit positionChanged(pos); });

  connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
    emit durationChanged(dur);
    updateCurrentItemFromMetaData();
  });

  connect(m_player, &QMediaPlayer::metaDataChanged, this,
          [this] { updateCurrentItemFromMetaData(); });

  connect(m_player, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &msg) {
            emit errorOccurred(msg);
          });

  connect(m_player, &QMediaPlayer::playbackStateChanged, this,
          [this](QMediaPlayer::PlaybackState st) {
            switch (st) {
            case QMediaPlayer::PlayingState:
              emit playbackStateChanged(PlaybackState::Playing);
              break;
            case QMediaPlayer::PausedState:
              emit playbackStateChanged(PlaybackState::Paused);
              break;
            case QMediaPlayer::StoppedState:
              emit playbackStateChanged(PlaybackState::Stopped);
              break;
            }

            // Start/stop fps estimation on play/pause/stop
            if (st == QMediaPlayer::PlayingState) {
              m_fpsTimer.restart();
              m_fpsTimerRunning = true;
              m_framesInWindow = 0;
            } else {
              m_fpsTimerRunning = false;
              m_framesInWindow = 0;
            }
          });

  connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus st) {
            switch (st) {
            case QMediaPlayer::NoMedia:
              emit statusChanged(Status::NoMedia);
              break;
            case QMediaPlayer::LoadingMedia:
              emit statusChanged(Status::Loading);
              break;
            case QMediaPlayer::LoadedMedia:
              emit statusChanged(Status::Loaded);
              break;
            case QMediaPlayer::BufferingMedia:
              emit statusChanged(Status::Buffering);
              break;
            case QMediaPlayer::EndOfMedia:
              emit statusChanged(Status::Ended);
              if (m_loop) {
                m_player->setPosition(0);
                m_player->play();
              } else {
                next();
              }
              break;
            case QMediaPlayer::InvalidMedia:
              emit statusChanged(Status::Error);
              break;
            default:
              break;
            }
          });
}

QtMediaBackend::~QtMediaBackend() {
  if (m_externalWindow) {
    m_externalWindow->close();
    delete m_externalWindow;
    m_externalWindow = nullptr;
  }
  // m_videoWidget is parented either to container or externalWindow, so Qt
  // deletes it with its parent.
}

void QtMediaBackend::setVideoContainer(QWidget *container) {
  m_container = container;
  if (!m_videoWidget) {
    m_videoWidget = new QVideoWidget(container);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Expanding);

    // IMPORTANT: let the layout manage geometry (same pattern as VLC backend)
    if (container->layout()) {
      container->layout()->addWidget(m_videoWidget);
    } else {
      auto *l = new QVBoxLayout(container);
      l->setContentsMargins(0, 0, 0, 0);
      l->addWidget(m_videoWidget);
    }

    m_videoWidget->show();

    m_player->setVideoOutput(m_videoWidget);

    // Frame tapping (does not break rendering)
    if (auto *sink = m_videoWidget->videoSink()) {
      connect(sink, &QVideoSink::videoFrameChanged, this,
              [this](const QVideoFrame &f) { onVideoFrameArrived(f); });
    }
  } else if (m_outputScreen == 0) {
    // re-embed if currently external
    m_videoWidget->setParent(container);
    if (container->layout())
      container->layout()->addWidget(m_videoWidget);
    m_videoWidget->show();
  }

  emit outputScreenChanged(m_outputScreen);
}

int QtMediaBackend::playlistSize() const { return m_playlist.size(); }
QList<PlaylistItem> QtMediaBackend::playlist() const { return m_playlist; }

void QtMediaBackend::clearPlaylist() {
  stop();
  m_playlist.clear();
  m_currentIndex = -1;
  emit playlistChanged();
  emit currentIndexChanged(m_currentIndex);
}

void QtMediaBackend::addToPlaylist(const QUrl &url) {
  PlaylistItem it;
  it.url = url;
  if (url.isLocalFile())
    it.displayName = QFileInfo(url.toLocalFile()).fileName();
  else
    it.displayName = url.toString();

  m_playlist.append(it);
  emit playlistChanged();

  if (m_currentIndex < 0) {
    setCurrentIndex(0);
  }
}

void QtMediaBackend::removeFromPlaylist(int index) {
  if (index < 0 || index >= m_playlist.size())
    return;

  const bool removingCurrent = (index == m_currentIndex);

  m_playlist.removeAt(index);

  if (m_playlist.isEmpty()) {
    stop();
    m_currentIndex = -1;
    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
    return;
  }

  if (removingCurrent) {
    // clamp current index
    m_currentIndex = std::min(index, int(m_playlist.size()) - 1);
    emit currentIndexChanged(m_currentIndex);
    setSourceFromCurrentIndex(false);
  } else if (index < m_currentIndex) {
    m_currentIndex--;
    emit currentIndexChanged(m_currentIndex);
  }

  emit playlistChanged();
}

void QtMediaBackend::movePlaylistItem(int from, int to) {
  if (from < 0 || from >= m_playlist.size())
    return;
  if (to < 0 || to >= m_playlist.size())
    return;
  if (from == to)
    return;

  auto item = m_playlist.takeAt(from);
  m_playlist.insert(to, item);

  if (m_currentIndex == from)
    m_currentIndex = to;
  else if (from < m_currentIndex && to >= m_currentIndex)
    m_currentIndex--;
  else if (from > m_currentIndex && to <= m_currentIndex)
    m_currentIndex++;

  emit playlistChanged();
  emit currentIndexChanged(m_currentIndex);
}

int QtMediaBackend::currentIndex() const { return m_currentIndex; }

void QtMediaBackend::setCurrentIndex(int index) {
  if (index < 0 || index >= m_playlist.size()) {
    m_currentIndex = -1;
    emit currentIndexChanged(m_currentIndex);
    return;
  }

  if (m_currentIndex == index)
    return;

  m_currentIndex = index;
  emit currentIndexChanged(m_currentIndex);

  setSourceFromCurrentIndex(false);
  emit subtitlesChanged();
}

void QtMediaBackend::setSourceFromCurrentIndex(bool autoplay) {
  if (m_currentIndex < 0 || m_currentIndex >= m_playlist.size())
    return;

  resetFpsDetection();

  const QUrl url = m_playlist[m_currentIndex].url;
  m_player->setSource(url);

  if (autoplay)
    m_player->play();
}

void QtMediaBackend::play() {
  if (m_currentIndex < 0) {
    if (!m_playlist.isEmpty())
      setCurrentIndex(0);
    else
      return;
  }

  if (m_player->source().isEmpty())
    setSourceFromCurrentIndex(false);

  m_player->play();
}

void QtMediaBackend::pause() { m_player->pause(); }
void QtMediaBackend::stop() { m_player->stop(); }

void QtMediaBackend::next() {
  if (m_playlist.isEmpty())
    return;
  if (m_currentIndex < 0) {
    setCurrentIndex(0);
    play();
    return;
  }

  int next = m_currentIndex + 1;
  if (next >= m_playlist.size()) {
    if (!m_loop)
      return;
    next = 0;
  }

  setCurrentIndex(next);
  play();
}

void QtMediaBackend::previous() {
  if (m_playlist.isEmpty())
    return;
  if (m_currentIndex < 0) {
    setCurrentIndex(0);
    play();
    return;
  }

  int prev = m_currentIndex - 1;
  if (prev < 0) {
    if (!m_loop)
      return;
    prev = m_playlist.size() - 1;
  }

  setCurrentIndex(prev);
  play();
}

bool QtMediaBackend::loopEnabled() const { return m_loop; }

void QtMediaBackend::setLoopEnabled(bool enabled) { m_loop = enabled; }

qint64 QtMediaBackend::positionMs() const { return m_player->position(); }
void QtMediaBackend::setPositionMs(qint64 ms) { m_player->setPosition(ms); }
qint64 QtMediaBackend::durationMs() const { return m_player->duration(); }

int QtMediaBackend::volume100() const {
  return clampInt(int(std::lround(m_audio->volume() * 100.0f)), 0, 100);
}

void QtMediaBackend::setVolume100(int v) {
  v = clampInt(v, 0, 100);
  m_audio->setVolume(float(v) / 100.0f);
}

int QtMediaBackend::screenCount() const {
  return QGuiApplication::screens().size();
}

QRect QtMediaBackend::screenGeometry(int screenIndex) const {
  const auto screens = QGuiApplication::screens();
  if (screenIndex < 0 || screenIndex >= screens.size())
    return {};
  return screens[screenIndex]->geometry();
}

int QtMediaBackend::outputScreen() const { return m_outputScreen; }

void QtMediaBackend::setOutputScreen(int screenIndex) {
  const auto screens = QGuiApplication::screens();
  if (screenIndex < 0 || screenIndex >= screens.size()) {
    qWarning() << "[QtMediaBackend] Invalid screen index:" << screenIndex;
    return;
  }

  if (!m_videoWidget) {
    m_outputScreen = screenIndex;
    emit outputScreenChanged(m_outputScreen);
    return;
  }

  if (screenIndex == 0) {
    // back to local
    m_outputScreen = 0;

    if (m_externalWindow) {
      m_videoWidget->setParent(m_container);
      m_videoWidget->setGeometry(m_container->rect());
      m_videoWidget->show();
      m_externalWindow->hide();
    } else {
      m_videoWidget->setParent(m_container);
      m_videoWidget->setGeometry(m_container->rect());
      m_videoWidget->show();
    }

    emit outputScreenChanged(m_outputScreen);
    return;
  }

  // external
  m_outputScreen = screenIndex;

  if (!m_externalWindow) {
    m_externalWindow = new QWidget(nullptr, Qt::Window);
    m_externalWindow->setStyleSheet("background:black;");
  }

  QScreen *screen = screens[screenIndex];
  m_externalWindow->setScreen(screen);
  m_externalWindow->setGeometry(screen->geometry());

  m_videoWidget->setParent(m_externalWindow);
  m_videoWidget->setGeometry(m_externalWindow->rect());
  m_videoWidget->show();

  m_externalWindow->showFullScreen();
  m_externalWindow->show();

  emit outputScreenChanged(m_outputScreen);
}

void QtMediaBackend::resetFpsDetection() {
  m_fpsMeta = 0.0;
  m_fpsEstimated = 0.0;
  m_reportedMeta = false;
  m_reportedEstimated = false;

  m_fpsTimerRunning = false;
  m_framesInWindow = 0;
}

void QtMediaBackend::reportFpsIfNeeded(const char *method, double fps) {
  emit fpsDetected(fps, QString::fromUtf8(method));
  qDebug().nospace() << "[QtMediaBackend] Detected framerate: " << fps
                     << " fps (source: " << method << ")";
}

void QtMediaBackend::updateCurrentItemFromMetaData() {
  if (m_currentIndex < 0 || m_currentIndex >= m_playlist.size())
    return;

  // duration
  const qint64 dur = m_player->duration();
  if (dur > 0)
    m_playlist[m_currentIndex].durationMs = dur;

  // resolution / fps (best-effort)
  const QMediaMetaData md = m_player->metaData();

  // Resolution key exists; value typically QSize
  const QVariant resVar = md.value(QMediaMetaData::Resolution);
  if (resVar.isValid()) {
    const QSize s = resVar.toSize();
    if (s.isValid()) {
      m_playlist[m_currentIndex].width = s.width();
      m_playlist[m_currentIndex].height = s.height();
      if (s.height() > 0) {
        m_playlist[m_currentIndex].aspectRatio =
            double(s.width()) / double(s.height());
      }
    }
  }

  // FPS metadata key (may be unavailable)
  const QVariant fpsVar = md.value(QMediaMetaData::VideoFrameRate);
  const double fps = fpsVar.toDouble();
  if (fps > 1.0 && fps < 240.0) {
    m_fpsMeta = fps;
    m_playlist[m_currentIndex].fps = fps;

    if (!m_reportedMeta) {
      m_reportedMeta = true;
      reportFpsIfNeeded("metadata (QMediaMetaData::VideoFrameRate)", fps);
    }
  }

  emit playlistChanged(); // refresh UI labels if you show metadata
}

void QtMediaBackend::onVideoFrameArrived(const QVideoFrame &) {
  // Estimate only if playing and metadata fps not already present
  if (m_player->playbackState() != QMediaPlayer::PlayingState)
    return;

  if (m_fpsMeta > 0.0)
    return;

  if (!m_fpsTimerRunning) {
    m_fpsTimer.restart();
    m_fpsTimerRunning = true;
    m_framesInWindow = 0;
    return;
  }

  ++m_framesInWindow;

  const qint64 elapsed = m_fpsTimer.elapsed();
  if (elapsed >= 2000) {
    const double fps = (m_framesInWindow * 1000.0) / double(elapsed);
    if (fps > 1.0 && fps < 240.0) {
      const double prev = m_fpsEstimated;
      m_fpsEstimated =
          (m_fpsEstimated <= 0.0) ? fps : (0.7 * m_fpsEstimated + 0.3 * fps);

      if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size())
        m_playlist[m_currentIndex].fps = m_fpsEstimated;

      const bool first = (prev <= 0.0);
      const bool changed = (!first && std::abs(m_fpsEstimated - prev) > 0.25);

      if (!m_reportedEstimated && first) {
        m_reportedEstimated = true;
        reportFpsIfNeeded(
            "estimated (QVideoSink frames / QElapsedTimer wall-clock)",
            m_fpsEstimated);
      } else if (changed) {
        // optional: comment out if you want less logs
        reportFpsIfNeeded(
            "estimated update (QVideoSink frames / QElapsedTimer wall-clock)",
            m_fpsEstimated);
      }

      emit playlistChanged();
    }

    m_fpsTimer.restart();
    m_framesInWindow = 0;
  }
}
