#include "test-card-dock.h"
#include <QHBoxLayout>
#include <QStyle>
#include <obs-module.h>

TestCardDock::TestCardDock(QWidget *parent) : QDialog(parent), globalSource(nullptr), isEnabled(false)
{
	setWindowTitle("Test Card Control");

	// Create buttons
	toggleButton = new QPushButton("TEST CARD", this);
	toggleButton->setCheckable(true);
	toggleButton->setMinimumWidth(120);

	settingsButton = new QPushButton("⚙ Config", this);
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
	connect(toggleButton, &QPushButton::clicked, this, &TestCardDock::onToggleClicked);
	connect(settingsButton, &QPushButton::clicked, this, &TestCardDock::onSettingsClicked);

	// Register frontend event callback for scene changes
	obs_frontend_add_event_callback(onFrontendEvent, this);

	// Create global source
	createGlobalSource();

	// Check initial state
	checkCurrentState();
	updateButtonState();
}

TestCardDock::~TestCardDock()
{
	// Remove callback
	obs_frontend_remove_event_callback(onFrontendEvent, this);

	if (globalSource) {
		obs_source_release(globalSource);
	}
}

void TestCardDock::createGlobalSource()
{
	// Check if source already exists
	globalSource = obs_get_source_by_name("__Test_Card_Global__");

	if (!globalSource) {
		// Create new source with correct ID
		obs_data_t *settings = obs_data_create();

		char *config_path = obs_module_get_config_path(obs_current_module(), "obs-test-card.json");
		if (config_path) {
			obs_data_t *data = obs_data_create_from_json_file(config_path);
			if (data) {
				const char *saved_text = obs_data_get_string(data, "custom_text");
				if (saved_text && *saved_text) {
					obs_data_set_string(settings, "custom_text", saved_text);
				}
				obs_data_release(data);
			}
			bfree(config_path);
		}

		globalSource = obs_source_create("test_source", "__Test_Card_Global__", settings, nullptr);
		obs_data_release(settings);

		blog(LOG_INFO, "[TestCardDock] Created new test card source");
	} else {
		blog(LOG_INFO, "[TestCardDock] Found existing test card source");
	}
}

void TestCardDock::checkCurrentState()
{
	// Check if test card is already in current scene
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (scene && globalSource) {
		obs_scene_t *obs_scene = obs_scene_from_source(scene);
		if (obs_scene) {
			obs_sceneitem_t *item = obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
			isEnabled = (item != nullptr);
			toggleButton->setChecked(isEnabled);

			blog(LOG_INFO, "[TestCardDock] Initial state: %s", isEnabled ? "ON" : "OFF");
		}
		obs_source_release(scene);
	}
}

void TestCardDock::onToggleClicked()
{
	if (!globalSource) {
		createGlobalSource();
		if (!globalSource) {
			blog(LOG_ERROR, "[TestCardDock] Failed to create source");
			return;
		}
	}

	isEnabled = toggleButton->isChecked();
	blog(LOG_INFO, "[TestCardDock] Toggle clicked: %s", isEnabled ? "ON" : "OFF");

	if (isEnabled) {
		addToCurrentScene();
	} else {
		removeFromAllScenes();
	}

	updateButtonState();
}

void TestCardDock::addToCurrentScene()
{
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

	// Check if already added
	obs_sceneitem_t *existing = obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));

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

			// Move to top of scene
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		} else {
			blog(LOG_ERROR, "[TestCardDock] Failed to add test card to scene");
		}
	}

	obs_source_release(scene);
}

void TestCardDock::removeFromCurrentScene()
{
	obs_source_t *scene = obs_frontend_get_current_scene();
	if (!scene) {
		return;
	}

	obs_scene_t *obs_scene = obs_scene_from_source(scene);
	if (obs_scene) {
		obs_sceneitem_t *item = obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
		if (item) {
			obs_sceneitem_remove(item);
			blog(LOG_INFO, "[TestCardDock] Removed test card from scene");
		}
	}

	obs_source_release(scene);
}

void TestCardDock::removeFromAllScenes()
{
	struct obs_frontend_source_list scenes = {0};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *scene_source = scenes.sources.array[i];
		obs_scene_t *obs_scene = obs_scene_from_source(scene_source);
		if (obs_scene) {
			obs_sceneitem_t *item = obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
			if (item) {
				obs_sceneitem_remove(item);
				blog(LOG_INFO, "[TestCardDock] Removed test card from scene: %s",
				     obs_source_get_name(scene_source));
			}
		}
	}

	obs_frontend_source_list_free(&scenes);
}

void TestCardDock::onFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	TestCardDock *dock = static_cast<TestCardDock *>(private_data);

	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		// When scene changes, auto-add test card if enabled
		if (dock->isEnabled) {
			blog(LOG_INFO, "[TestCardDock] Scene changed, adding test card to new scene");
			dock->addToCurrentScene();
		}
	}
}

void TestCardDock::onSettingsClicked()
{
	if (!globalSource)
		return;

	obs_frontend_open_source_properties(globalSource);
}

void TestCardDock::updateButtonState()
{
	if (isEnabled) {
		toggleButton->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
	} else {
		toggleButton->setStyleSheet("");
	}
}
