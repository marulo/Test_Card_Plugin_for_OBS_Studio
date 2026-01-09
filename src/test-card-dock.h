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

  QPushButton *toggleButton;
  QPushButton *settingsButton;
  obs_source_t *globalSource;
  bool isEnabled;
};
