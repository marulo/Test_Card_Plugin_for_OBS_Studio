#include "test-card-dock.h"
#include <QHBoxLayout>
#include <QStyle>

TestCardDock::TestCardDock(QWidget *parent)
    : QDialog(parent), globalSource(nullptr), isEnabled(false) {
  setWindowTitle("Test Card Control");

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

  // Check initial state
  checkCurrentState();
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
    // Create new source with correct ID
    obs_data_t *settings = obs_data_create();
    globalSource = obs_source_create("test_source", "__Test_Card_Global__",
                                     settings, nullptr);
    obs_data_release(settings);

    blog(LOG_INFO, "[TestCardDock] Created new test card source");
  } else {
    blog(LOG_INFO, "[TestCardDock] Found existing test card source");
  }
}

void TestCardDock::checkCurrentState() {
  // Check if test card is already in current scene
  obs_source_t *scene = obs_frontend_get_current_scene();
  if (scene && globalSource) {
    obs_scene_t *obs_scene = obs_scene_from_source(scene);
    if (obs_scene) {
      obs_sceneitem_t *item =
          obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
      isEnabled = (item != nullptr);
      toggleButton->setChecked(isEnabled);

      blog(LOG_INFO, "[TestCardDock] Initial state: %s",
           isEnabled ? "ON" : "OFF");
    }
    obs_source_release(scene);
  }
}

void TestCardDock::onToggleClicked() {
  if (!globalSource) {
    createGlobalSource();
    if (!globalSource) {
      blog(LOG_ERROR, "[TestCardDock] Failed to create source");
      return;
    }
  }

  isEnabled = toggleButton->isChecked();
  blog(LOG_INFO, "[TestCardDock] Toggle clicked: %s", isEnabled ? "ON" : "OFF");

  obs_source_t *scene = obs_frontend_get_current_scene();
  if (!scene) {
    blog(LOG_ERROR, "[TestCardDock] No current scene");
    return;
  }

  obs_scene_t *obs_scene = obs_scene_from_source(scene);
  if (!obs_scene) {
    blog(LOG_ERROR, "[TestCardDock] Could not get obs_scene");
    obs_source_release(scene);
    return;
  }

  if (isEnabled) {
    // Add to current scene
    obs_sceneitem_t *existing =
        obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));

    if (existing) {
      blog(LOG_INFO, "[TestCardDock] Test card already in scene");
    } else {
      // Add as scene item
      obs_sceneitem_t *item = obs_scene_add(obs_scene, globalSource);
      if (item) {
        blog(LOG_INFO, "[TestCardDock] Added test card to scene");

        // Position at top-left, fullscreen
        struct vec2 pos = {0, 0};
        obs_sceneitem_set_pos(item, &pos);

        // Get canvas size for scale
        obs_video_info ovi;
        if (obs_get_video_info(&ovi)) {
          uint32_t source_w = obs_source_get_width(globalSource);
          uint32_t source_h = obs_source_get_height(globalSource);

          if (source_w > 0 && source_h > 0) {
            struct vec2 scale;
            scale.x = (float)ovi.base_width / (float)source_w;
            scale.y = (float)ovi.base_height / (float)source_h;
            obs_sceneitem_set_scale(item, &scale);
          }
        }
      } else {
        blog(LOG_ERROR, "[TestCardDock] Failed to add test card to scene");
      }
    }
  } else {
    // Remove from current scene
    obs_sceneitem_t *item =
        obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
    if (item) {
      obs_sceneitem_remove(item);
      blog(LOG_INFO, "[TestCardDock] Removed test card from scene");
    } else {
      blog(LOG_WARNING, "[TestCardDock] Test card not found in scene");
    }
  }

  obs_source_release(scene);
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
