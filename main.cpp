#include <QApplication>

#include "player/PlayerWidget.h"
#include "player/VlcBackend.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  IPlayerBackend *backend = new VlcBackend();
  app.setApplicationDisplayName("CleanPlayer");

  PlayerWidget w(backend);
  w.resize(660, 850);
  w.show();

  return app.exec();
}
