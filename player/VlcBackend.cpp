#include "VlcBackend.h"

#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <vlc/vlc.h>

#include <algorithm>

static int clampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static QString safeFromUtf8(const char *s) {
  return s ? QString::fromUtf8(s) : QString();
}

VlcBackend::VlcBackend(QObject *parent) : IPlayerBackend(parent) {
  const char *vlc_args[] = {"--no-xlib", "--quiet", "--no-sub-autodetect-file"};

  m_vlc = libvlc_new(int(sizeof(vlc_args) / sizeof(vlc_args[0])), vlc_args);
  if (!m_vlc) {
    emit errorOccurred("Failed to create libVLC instance");
    return;
  }

  m_mp = libvlc_media_player_new(m_vlc);
  if (!m_mp) {
    emit errorOccurred("Failed to create libVLC media player");
    return;
  }

  installVlcEvents();

  m_pollTimer.setInterval(250);
  connect(&m_pollTimer, &QTimer::timeout, this,
          [this] { pollPositionDuration(); });

  m_statsTimer.setInterval(1000);
  connect(&m_statsTimer, &QTimer::timeout, this, [this] { pollStats(); });
}

VlcBackend::~VlcBackend() {
  m_pollTimer.stop();
  m_statsTimer.stop();

  if (m_mp) {
    libvlc_media_player_stop(m_mp);

#ifdef Q_OS_WIN
    libvlc_media_player_set_hwnd(m_mp, nullptr);
#elif defined(Q_OS_MAC)
    libvlc_media_player_set_nsobject(m_mp, nullptr);
#else
    libvlc_media_player_set_xwindow(m_mp, 0);
#endif

    libvlc_media_player_release(m_mp);
    m_mp = nullptr;
  }

  if (m_vlc) {
    libvlc_release(m_vlc);
    m_vlc = nullptr;
  }

  if (m_externalWindow) {
    m_externalWindow->close();
    delete m_externalWindow;
    m_externalWindow = nullptr;
  }
}

void VlcBackend::setVideoContainer(QWidget *container) {
  m_container = container;

  if (!m_renderWidget) {
    m_renderWidget = new QWidget(container);
    m_renderWidget->setAttribute(Qt::WA_NativeWindow);
    m_renderWidget->setAttribute(Qt::WA_DontCreateNativeAncestors);
    m_renderWidget->setStyleSheet("background:black;");

    if (container->layout())
      container->layout()->addWidget(m_renderWidget);
    else {
      auto *l = new QVBoxLayout(container);
      l->setContentsMargins(0, 0, 0, 0);
      l->addWidget(m_renderWidget);
    }

    m_renderWidget->show();
    bindVlcToWidget(m_renderWidget);
  } else if (m_outputScreen == 0) {
    m_renderWidget->setParent(container);
    if (container->layout())
      container->layout()->addWidget(m_renderWidget);
    m_renderWidget->show();
    bindVlcToWidget(m_renderWidget);
  }

  emit outputScreenChanged(m_outputScreen);
}

void VlcBackend::bindVlcToWidget(QWidget *w) {
  if (!m_mp || !w)
    return;

  w->winId();

#ifdef Q_OS_WIN
  libvlc_media_player_set_hwnd(m_mp, (void *)w->winId());
#elif defined(Q_OS_MAC)
  libvlc_media_player_set_nsobject(m_mp, (void *)w->winId());
#else
  libvlc_media_player_set_xwindow(m_mp, static_cast<unsigned long>(w->winId()));
#endif
}

int VlcBackend::playlistSize() const { return m_playlist.size(); }
QList<PlaylistItem> VlcBackend::playlist() const { return m_playlist; }

void VlcBackend::clearPlaylist() {
  stop();
  m_playlist.clear();
  m_subCache.clear();
  m_currentIndex = -1;
  emit playlistChanged();
  emit currentIndexChanged(m_currentIndex);
  emit subtitlesChanged();
}

void VlcBackend::addToPlaylist(const QUrl &url) {
  PlaylistItem it;
  it.url = url;
  it.displayName = url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName()
                                     : url.toString();

  m_playlist.append(it);

  SubtitleCache sc;
  sc.valid = false;
  m_subCache.append(sc);

  emit playlistChanged();

  if (m_currentIndex < 0)
    setCurrentIndex(0);
}

void VlcBackend::removeFromPlaylist(int index) {
  if (index < 0 || index >= m_playlist.size())
    return;

  const bool removingCurrent = (index == m_currentIndex);

  m_playlist.removeAt(index);
  m_subCache.removeAt(index);

  if (m_playlist.isEmpty()) {
    stop();
    m_currentIndex = -1;
    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
    emit subtitlesChanged();
    return;
  }

  if (removingCurrent) {
    m_currentIndex = std::min(index, int(m_playlist.size()) - 1);
    emit currentIndexChanged(m_currentIndex);
    setSourceFromCurrentIndex(false);
    refreshSubtitlesForCurrentMedia();
    applyCachedSubtitleToPlayer();
    emit subtitlesChanged();
  } else if (index < m_currentIndex) {
    m_currentIndex--;
    emit currentIndexChanged(m_currentIndex);
  }

  emit playlistChanged();
}

void VlcBackend::movePlaylistItem(int from, int to) {
  if (from < 0 || from >= m_playlist.size())
    return;
  if (to < 0 || to >= m_playlist.size())
    return;
  if (from == to)
    return;

  auto item = m_playlist.takeAt(from);
  m_playlist.insert(to, item);

  auto sc = m_subCache.takeAt(from);
  m_subCache.insert(to, sc);

  if (m_currentIndex == from)
    m_currentIndex = to;
  else if (from < m_currentIndex && to >= m_currentIndex)
    m_currentIndex--;
  else if (from > m_currentIndex && to <= m_currentIndex)
    m_currentIndex++;

  emit playlistChanged();
  emit currentIndexChanged(m_currentIndex);
  emit subtitlesChanged();
}

int VlcBackend::currentIndex() const { return m_currentIndex; }

void VlcBackend::setCurrentIndex(int index) {
  if (index < 0 || index >= m_playlist.size()) {
    m_currentIndex = -1;
    emit currentIndexChanged(m_currentIndex);
    emit subtitlesChanged();
    return;
  }
  if (m_currentIndex == index)
    return;

  m_currentIndex = index;
  emit currentIndexChanged(m_currentIndex);

  setSourceFromCurrentIndex(false);

  refreshSubtitlesForCurrentMedia();
  applyCachedSubtitleToPlayer();
  emit subtitlesChanged();
}

void VlcBackend::setSourceFromCurrentIndex(bool autoplay) {
  if (!m_mp)
    return;
  if (m_currentIndex < 0 || m_currentIndex >= m_playlist.size())
    return;

  const QUrl url = m_playlist[m_currentIndex].url;
  if (!url.isLocalFile()) {
    emit errorOccurred(
        "VlcBackend currently expects local files (QUrl::fromLocalFile).");
    return;
  }

  const QString path = url.toLocalFile();
  libvlc_media_t *m = libvlc_media_new_path(m_vlc, path.toUtf8().constData());
  if (!m) {
    emit errorOccurred("Failed to create VLC media.");
    return;
  }

  libvlc_media_player_set_media(m_mp, m);

  libvlc_media_parse_with_options(m, libvlc_media_parse_local, -1);

  libvlc_media_track_t **tracks = nullptr;
  const int tcount = libvlc_media_tracks_get(m, &tracks);

  double fps = 0.0;
  int w = 0, h = 0;
  double ar = 0.0;

  for (int i = 0; i < tcount; ++i) {
    if (tracks[i]->i_type == libvlc_track_video && tracks[i]->video) {
      const auto *v = tracks[i]->video;
      w = int(v->i_width);
      h = int(v->i_height);
      if (v->i_frame_rate_den != 0) {
        fps = double(v->i_frame_rate_num) / double(v->i_frame_rate_den);
      }
      double sar = 1.0;
      if (v->i_sar_den > 0)
        sar = double(v->i_sar_num) / double(v->i_sar_den);
      if (h > 0)
        ar = (double(w) / double(h)) * sar;
    }
  }

  if (tracks)
    libvlc_media_tracks_release(tracks, tcount);

  const qint64 dur = libvlc_media_get_duration(m);

  if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
    m_playlist[m_currentIndex].durationMs = dur;
    m_playlist[m_currentIndex].fps = fps;
    m_playlist[m_currentIndex].width = w;
    m_playlist[m_currentIndex].height = h;
    m_playlist[m_currentIndex].aspectRatio = ar;
    emit playlistChanged();

    if (fps > 1.0 && fps < 240.0)
      emit fpsDetected(fps, "libVLC track parse (libvlc_media_tracks_get)");
  }

  libvlc_media_release(m);

  if (autoplay)
    libvlc_media_player_play(m_mp);
}

void VlcBackend::play() {
  if (!m_mp)
    return;

  if (m_currentIndex < 0) {
    if (!m_playlist.isEmpty())
      setCurrentIndex(0);
    else
      return;
  }

  libvlc_media_t *cur = libvlc_media_player_get_media(m_mp);
  if (!cur) {
    setSourceFromCurrentIndex(true);
  } else {
    libvlc_media_player_play(m_mp);
  }

  m_pollTimer.start();
  m_statsTimer.start();
  emitStateFromVlcState();

  QTimer::singleShot(200, this, [this] {
    refreshSubtitlesForCurrentMedia();
    applyCachedSubtitleToPlayer();
    emit subtitlesChanged();
  });
}

void VlcBackend::pause() {
  if (!m_mp)
    return;
  libvlc_media_player_set_pause(m_mp, 1);
  emitStateFromVlcState();
}

void VlcBackend::stop() {
  if (!m_mp)
    return;
  libvlc_media_player_stop(m_mp);
  m_pollTimer.stop();
  m_statsTimer.stop();
  emitStateFromVlcState();
}

void VlcBackend::next() {
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

void VlcBackend::previous() {
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

bool VlcBackend::loopEnabled() const { return m_loop; }
void VlcBackend::setLoopEnabled(bool enabled) { m_loop = enabled; }

qint64 VlcBackend::positionMs() const {
  if (!m_mp)
    return 0;
  return libvlc_media_player_get_time(m_mp);
}

void VlcBackend::setPositionMs(qint64 ms) {
  if (!m_mp)
    return;
  libvlc_media_player_set_time(m_mp, ms);
  emit positionChanged(ms);
}

qint64 VlcBackend::durationMs() const {
  if (!m_mp)
    return -1;
  return libvlc_media_player_get_length(m_mp);
}

int VlcBackend::volume100() const {
  if (!m_mp)
    return 0;
  return clampInt(libvlc_audio_get_volume(m_mp), 0, 100);
}

void VlcBackend::setVolume100(int v) {
  if (!m_mp)
    return;
  v = clampInt(v, 0, 100);
  libvlc_audio_set_volume(m_mp, v);
}

int VlcBackend::screenCount() const {
  return QGuiApplication::screens().size();
}

QRect VlcBackend::screenGeometry(int screenIndex) const {
  const auto screens = QGuiApplication::screens();
  if (screenIndex < 0 || screenIndex >= screens.size())
    return {};
  return screens[screenIndex]->geometry();
}

int VlcBackend::outputScreen() const { return m_outputScreen; }

void VlcBackend::setOutputScreen(int screenIndex) {
  const auto screens = QGuiApplication::screens();
  if (screenIndex < 0 || screenIndex >= screens.size()) {
    qWarning() << "[VlcBackend] Invalid screen index:" << screenIndex;
    return;
  }

  if (!m_renderWidget) {
    m_outputScreen = screenIndex;
    emit outputScreenChanged(m_outputScreen);
    return;
  }

  if (screenIndex == 0) {
    m_outputScreen = 0;

    if (m_externalWindow) {
      m_renderWidget->setParent(m_container);
      if (m_container->layout())
        m_container->layout()->addWidget(m_renderWidget);
      m_renderWidget->show();
      m_externalWindow->hide();
    } else {
      m_renderWidget->setParent(m_container);
      if (m_container->layout())
        m_container->layout()->addWidget(m_renderWidget);
      m_renderWidget->show();
    }

    bindVlcToWidget(m_renderWidget);
    emit outputScreenChanged(m_outputScreen);
    return;
  }

  m_outputScreen = screenIndex;

  if (!m_externalWindow) {
    m_externalWindow = new QWidget(nullptr, Qt::Window);
    m_externalWindow->setStyleSheet("background:black;");
    m_externalWindow->setLayout(new QVBoxLayout());
    m_externalWindow->layout()->setContentsMargins(0, 0, 0, 0);
  }

  QScreen *screen = screens[screenIndex];
  m_externalWindow->setScreen(screen);
  m_externalWindow->setGeometry(screen->geometry());

  m_renderWidget->setParent(m_externalWindow);
  m_externalWindow->layout()->addWidget(m_renderWidget);
  m_renderWidget->show();

  bindVlcToWidget(m_renderWidget);

  m_externalWindow->showFullScreen();
  m_externalWindow->show();

  emit outputScreenChanged(m_outputScreen);
}

// ---- Subtitles capability ----

QStringList VlcBackend::subtitleTracks() const {
  if (m_currentIndex < 0 || m_currentIndex >= m_subCache.size())
    return {};
  if (!m_subCache[m_currentIndex].valid)
    return {};
  return m_subCache[m_currentIndex].names;
}

int VlcBackend::currentSubtitleTrack() const {
  if (m_currentIndex < 0 || m_currentIndex >= m_subCache.size())
    return -1;
  return m_subCache[m_currentIndex].currentIndex;
}

bool VlcBackend::setSubtitleTrack(int index) {
  if (!m_mp)
    return false;
  if (m_currentIndex < 0 || m_currentIndex >= m_subCache.size())
    return false;

  auto &cache = m_subCache[m_currentIndex];

  if (!cache.valid)
    refreshSubtitlesForCurrentMedia();

  // allow off always
  if (index == -1) {
    cache.currentIndex = -1;
    applyCachedSubtitleToPlayer();
    emit subtitlesChanged();
    return true;
  }

  if (!cache.valid)
    return false;
  if (index < 0 || index >= cache.spuIds.size())
    return false;

  cache.currentIndex = index;
  applyCachedSubtitleToPlayer();
  emit subtitlesChanged();
  return true;
}

void VlcBackend::refreshSubtitlesForCurrentMedia() {
  if (!m_mp)
    return;
  if (m_currentIndex < 0 || m_currentIndex >= m_subCache.size())
    return;

  auto &cache = m_subCache[m_currentIndex];
  cache.names.clear();
  cache.spuIds.clear();
  cache.valid = false;

  libvlc_media_t *m = libvlc_media_player_get_media(m_mp);
  if (!m)
    return;

  libvlc_track_description_t *desc = libvlc_video_get_spu_description(m_mp);
  if (!desc)
    return;

  for (libvlc_track_description_t *d = desc; d != nullptr; d = d->p_next) {
    const int id = d->i_id;
    const QString name = safeFromUtf8(d->psz_name);

    if (id >= 0) {
      cache.spuIds.push_back(id);
      cache.names.append(name.isEmpty() ? QString("Subtitle %1").arg(id)
                                        : name);
    }
  }

  libvlc_track_description_list_release(desc);

  cache.valid = !cache.names.isEmpty();

  const int currentSpuId = libvlc_video_get_spu(m_mp);
  cache.currentIndex = -1;
  for (int i = 0; i < cache.spuIds.size(); ++i) {
    if (cache.spuIds[i] == currentSpuId) {
      cache.currentIndex = i;
      break;
    }
  }

  emit subtitlesChanged();
}

void VlcBackend::applyCachedSubtitleToPlayer() {
  if (!m_mp)
    return;
  if (m_currentIndex < 0 || m_currentIndex >= m_subCache.size())
    return;

  auto &cache = m_subCache[m_currentIndex];

  if (cache.currentIndex == -1) {
    libvlc_video_set_spu(m_mp, -1);
    return;
  }

  if (!cache.valid)
    return;

  if (cache.currentIndex >= 0 && cache.currentIndex < cache.spuIds.size()) {
    const int spuId = cache.spuIds[cache.currentIndex];
    libvlc_video_set_spu(m_mp, spuId);
  }
}

// ---- Polling helpers ----

void VlcBackend::pollPositionDuration() {
  if (!m_mp)
    return;

  const qint64 pos = libvlc_media_player_get_time(m_mp);
  emit positionChanged(pos);

  const qint64 dur = libvlc_media_player_get_length(m_mp);
  if (dur != m_lastDuration) {
    m_lastDuration = dur;
    emit durationChanged(dur);
  }
}

void VlcBackend::pollStats() {
  if (!m_mp)
    return;

  libvlc_media_t *m = libvlc_media_player_get_media(m_mp);
  if (!m)
    return;

  libvlc_media_stats_t st{};
  const int ok = libvlc_media_get_stats(m, &st);
  if (!ok)
    return;

  const int lost = int(st.i_lost_pictures);
  const int displayed = int(st.i_displayed_pictures);

  emit framesDropped(lost, displayed);

  if (m_lastLostPictures < 0)
    m_lastLostPictures = lost;
  if (m_lastDisplayedPictures < 0)
    m_lastDisplayedPictures = displayed;

  m_lastLostPictures = lost;
  m_lastDisplayedPictures = displayed;
}

// ---- VLC events ----

void VlcBackend::installVlcEvents() {
  if (!m_mp)
    return;

  libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_mp);
  libvlc_event_attach(em, libvlc_MediaPlayerPlaying,
                      &VlcBackend::handleVlcEvent, this);
  libvlc_event_attach(em, libvlc_MediaPlayerPaused, &VlcBackend::handleVlcEvent,
                      this);
  libvlc_event_attach(em, libvlc_MediaPlayerStopped,
                      &VlcBackend::handleVlcEvent, this);
  libvlc_event_attach(em, libvlc_MediaPlayerEndReached,
                      &VlcBackend::handleVlcEvent, this);
  libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError,
                      &VlcBackend::handleVlcEvent, this);
}

void VlcBackend::handleVlcEvent(const libvlc_event_t *ev, void *ud) {
  auto *self = static_cast<VlcBackend *>(ud);
  if (!self)
    return;

  QMetaObject::invokeMethod(
      self,
      [self, evType = ev->type] {
        switch (evType) {
        case libvlc_MediaPlayerPlaying:
          self->m_pollTimer.start();
          self->m_statsTimer.start();
          emit self->statusChanged(Status::Loaded);
          emit self->playbackStateChanged(PlaybackState::Playing);

          self->refreshSubtitlesForCurrentMedia();
          self->applyCachedSubtitleToPlayer();
          emit self->subtitlesChanged();
          break;

        case libvlc_MediaPlayerPaused:
          emit self->playbackStateChanged(PlaybackState::Paused);
          break;

        case libvlc_MediaPlayerStopped:
          self->m_pollTimer.stop();
          self->m_statsTimer.stop();
          emit self->playbackStateChanged(PlaybackState::Stopped);
          break;

        case libvlc_MediaPlayerEndReached:
          emit self->statusChanged(Status::Ended);
          self->next();
          break;

        case libvlc_MediaPlayerEncounteredError:
          emit self->statusChanged(Status::Error);
          emit self->errorOccurred("VLC playback error");
          break;

        default:
          break;
        }
      },
      Qt::QueuedConnection);
}

void VlcBackend::emitStateFromVlcState() {
  if (!m_mp)
    return;

  const libvlc_state_t st = libvlc_media_player_get_state(m_mp);
  switch (st) {
  case libvlc_Playing:
    emit playbackStateChanged(PlaybackState::Playing);
    break;
  case libvlc_Paused:
    emit playbackStateChanged(PlaybackState::Paused);
    break;
  default:
    emit playbackStateChanged(PlaybackState::Stopped);
    break;
  }
}
