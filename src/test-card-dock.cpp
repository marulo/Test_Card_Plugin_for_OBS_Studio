#include "test-card-dock.h"
#include <QHBoxLayout>
#include <QStyle>

TestCardDock::TestCardDock(QWidget *parent)
    : QWidget(parent), globalSource(nullptr), isEnabled(false) {
  // Create buttons
  toggleButton = new QPushButton("TEST CARD", this);
  toggleButton->setCheckable(true);
  toggleButton->setMinimumWidth(120);

  settingsButton = new QPushButton("⋮", this);
  settingsButton->setMaximumWidth(30);
  settingsButton->setToolTip("Settings");

  // Create layout
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  layout->addWidget(toggleButton);
  layout->addWidget(settingsButton);
  layout->addStretch();

  setLayout(layout);

  // Connect signals
  connect(toggleButton, &QPushButton::clicked, this,
          &TestCardDock::onToggleClicked);
  connect(settingsButton, &QPushButton::clicked, this,
          &TestCardDock::onSettingsClicked);

  // Create global source
  createGlobalSource();
  updateButtonState();
}

TestCardDock::~TestCardDock() {
  if (globalSource) {
    obs_source_release(globalSource);
  }
}

void TestCardDock::createGlobalSource() {
  // Check if source already exists
  globalSource = obs_get_source_by_name("__Test_Card_Global__");

  if (!globalSource) {
    // Create new source
    obs_data_t *settings = obs_data_create();
    globalSource = obs_source_create("test_card_source", "__Test_Card_Global__",
                                     settings, nullptr);
    obs_data_release(settings);
  }
}

void TestCardDock::onToggleClicked() {
  if (!globalSource) {
    createGlobalSource();
    if (!globalSource)
      return;
  }

  isEnabled = toggleButton->isChecked();

  // Toggle visibility - for now we just enable/disable
  // In full implementation, this would add/remove from a global scene
  obs_source_set_enabled(globalSource, isEnabled);

  updateButtonState();
}

void TestCardDock::onSettingsClicked() {
  if (!globalSource)
    return;

  obs_frontend_open_source_properties(globalSource);
}

void TestCardDock::updateButtonState() {
  if (isEnabled) {
    toggleButton->setStyleSheet(
        "background-color: #4CAF50; color: white; font-weight: bold;");
  } else {
    toggleButton->setStyleSheet("");
  }
}
