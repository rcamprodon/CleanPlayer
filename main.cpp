#include <QApplication>
#include <QStringList>

#include "player/PlayerWidget.h"
#include "player/QtMediaBackend.h"
#include "player/VlcBackend.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  const QStringList args = app.arguments();
  const bool useVlc = args.contains("--vlc");

  IPlayerBackend *backend = nullptr;
  if (useVlc) {
    backend = new VlcBackend();
    app.setApplicationDisplayName("CleanPlayer (libVLC)");
  } else {
    backend = new QtMediaBackend();
    app.setApplicationDisplayName("CleanPlayer (QtMediaPlayer)");
  }
  PlayerWidget w(backend);
  w.resize(660, 850);
  w.show();

  return app.exec();
}
