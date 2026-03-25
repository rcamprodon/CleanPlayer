#include <QApplication>

#include "player/PlayerWidget.h"
#include "player/VlcBackend.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  app.setApplicationDisplayName("CleanPlayer");

  // backend must outlive the widget; use a block so the PlayerWidget (and its
  // render widget) is fully destroyed before we delete the backend, ensuring
  // VLC is detached from the native window before cleanup.
  auto *backend = new VlcBackend();
  int result = 0;
  {
    PlayerWidget w(backend);
    w.resize(660, 850);
    w.show();
    result = app.exec();
  }
  delete backend;
  return result;
}
