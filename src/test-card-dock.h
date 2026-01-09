#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>


#include <obs-frontend-api.h>
#include <obs.h>


class TestCardDock : public QDialog {
  Q_OBJECT

public:
  explicit TestCardDock(QWidget *parent = nullptr);
  ~TestCardDock();

private slots:
  void onToggleClicked();
  void onSettingsClicked();

private:
  void createGlobalSource();
  void checkCurrentState();
  void updateButtonState();
  void addToCurrentScene();
  void removeFromCurrentScene();

  static void onFrontendEvent(enum obs_frontend_event event,
                              void *private_data);

  QPushButton *toggleButton;
  QPushButton *settingsButton;
  obs_source_t *globalSource;
  bool isEnabled;
};
