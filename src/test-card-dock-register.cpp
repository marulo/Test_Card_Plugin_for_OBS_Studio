#include "test-card-dock.h"

#ifdef ENABLE_QT

#include <obs-frontend-api.h>

extern "C" void register_test_card_dock() {
  // Create and register the dock
  TestCardDock *dock = new TestCardDock();
  dock->setObjectName("TestCardDock");
  dock->setWindowTitle("Test Card");

  // Use Qt's dock system
  obs_frontend_add_dock((void *)dock);
}

#else

extern "C" void register_test_card_dock() {
  // Qt not enabled, do nothing
}

#endif
