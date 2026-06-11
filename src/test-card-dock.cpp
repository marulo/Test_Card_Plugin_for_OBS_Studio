#include "test-card-dock.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <obs-module.h>
#include <graphics/graphics.h>

// ---------------------------------------------------------------------------
// Aggressive Cleanup Helper (Exterminates by ID)
// ---------------------------------------------------------------------------
static bool remove_stale_items_callback(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	bool *found_any = (bool *)param;
	obs_source_t *item_source = obs_sceneitem_get_source(item);

	if (item_source && strcmp(obs_source_get_id(item_source), "test_source") == 0) {
		obs_sceneitem_remove(item);
		if (found_any)
			*found_any = true;
	}

	if (obs_sceneitem_is_group(item)) {
		obs_scene_t *group_scene = obs_sceneitem_group_get_scene(item);
		if (group_scene) {
			obs_scene_enum_items(group_scene, remove_stale_items_callback, param);
		}
	}

	return true;
}

static bool cleanup_all_sources_callback(void *param, obs_source_t *source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene) {
		obs_scene_enum_items(scene, remove_stale_items_callback, param);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Scene Injector
// ---------------------------------------------------------------------------
static void inject_into_scene(obs_source_t *scene_source, obs_source_t *target_source)
{
	if (!scene_source || !target_source)
		return;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		blog(LOG_INFO, "[TestCardDock] inject_into_scene: Failed because scene_source is not a scene! (ID: %s)",
		     obs_source_get_id(scene_source));
		return;
	}

	// Check if already in the scene
	bool found = false;
	auto check_cb = [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
		obs_source_t *item_src = obs_sceneitem_get_source(item);
		if (item_src && strcmp(obs_source_get_id(item_src), "test_source") == 0) {
			*(bool *)param = true;
			return false; // stop iterating
		}
		return true;
	};
	obs_scene_enum_items(scene, check_cb, &found);

	if (!found) {
		obs_sceneitem_t *item = obs_scene_add(scene, target_source);
		if (item) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			blog(LOG_INFO, "[TestCardDock] inject_into_scene: Added to %s",
			     obs_source_get_name(scene_source));
		} else {
			blog(LOG_INFO, "[TestCardDock] inject_into_scene: Failed to add to %s",
			     obs_source_get_name(scene_source));
		}
	} else {
		blog(LOG_INFO, "[TestCardDock] inject_into_scene: Already exists in %s",
		     obs_source_get_name(scene_source));

		// fallback check by enumerating and forcing visibility
		auto force_vis_cb = [](obs_scene_t *, obs_sceneitem_t *item, void *) -> bool {
			obs_source_t *item_src = obs_sceneitem_get_source(item);
			if (item_src && strcmp(obs_source_get_id(item_src), "test_source") == 0) {
				obs_sceneitem_set_visible(item, true);
				obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			}
			return true;
		};
		obs_scene_enum_items(scene, force_vis_cb, nullptr);
		blog(LOG_INFO, "[TestCardDock] inject_into_scene: Forced visibility via enumeration in %s",
		     obs_source_get_name(scene_source));
	}
}

// ---------------------------------------------------------------------------
// TestCardDock
// ---------------------------------------------------------------------------

TestCardDock::TestCardDock(QWidget *parent) : QDialog(parent), globalSource(nullptr), isEnabled(false)
{
	setWindowTitle("Test Card Control");

	toggleButton = new QPushButton("TEST CARD", this);
	toggleButton->setCheckable(true);
	toggleButton->setMinimumWidth(120);

	settingsButton = new QPushButton("⚙ Config", this);
	settingsButton->setToolTip("Settings");

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(toggleButton);
	layout->addWidget(settingsButton);
	layout->addStretch();
	setLayout(layout);

	connect(toggleButton, &QPushButton::clicked, this, &TestCardDock::onToggleClicked);
	connect(settingsButton, &QPushButton::clicked, this, &TestCardDock::onSettingsClicked);

	obs_frontend_add_event_callback(onFrontendEvent, this);
	createGlobalSource();
	updateButtonState();
}

TestCardDock::~TestCardDock()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);

	if (isEnabled && globalSource) {
		obs_source_set_enabled(globalSource, false);
		isEnabled = false;
	}

	if (globalSource) {
		obs_source_release(globalSource);
		globalSource = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Source lifecycle
// ---------------------------------------------------------------------------

void TestCardDock::createGlobalSource()
{
	globalSource = obs_get_source_by_name("__Test_Card_Global__");

	if (!globalSource) {
		obs_data_t *settings = obs_data_create();
		globalSource = obs_source_create("test_source", "__Test_Card_Global__", settings, nullptr);
		obs_data_release(settings);
	}

	// Ensure it starts disabled
	if (globalSource && !isEnabled) {
		obs_source_set_enabled(globalSource, false);
	}
}

// ---------------------------------------------------------------------------
// Overlay Activation
// ---------------------------------------------------------------------------

void TestCardDock::activateTestCard()
{
	if (!globalSource) {
		createGlobalSource();
		if (!globalSource)
			return;
	}

	obs_source_set_enabled(globalSource, true);

	QString debugInfo = "DEBUG INFO V0.4.7\n\n";

	obs_source_t *program_scene = obs_frontend_get_current_scene();
	if (program_scene) {
		blog(LOG_INFO, "[TestCardDock] Program scene name: %s", obs_source_get_name(program_scene));
		debugInfo += QString("Program Scene: %1\n").arg(obs_source_get_name(program_scene));
		obs_scene_t *ps = obs_scene_from_source(program_scene);
		if (ps) {
			obs_sceneitem_t *item = obs_scene_add(ps, globalSource);
			if (item) {
				obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
				obs_sceneitem_set_visible(item, true);
				debugInfo += "  -> Successfully added to Program Scene!\n";
			} else {
				debugInfo += "  -> FAILED to add to Program Scene (obs_scene_add returned null)\n";
			}
		} else {
			debugInfo += "  -> FAILED: Program source is not a valid scene\n";
		}
		obs_source_release(program_scene);
	} else {
		debugInfo += "Program Scene is NULL!\n";
	}

	obs_source_t *preview_scene = obs_frontend_get_current_preview_scene();
	if (preview_scene) {
		debugInfo += QString("\nPreview Scene: %1\n").arg(obs_source_get_name(preview_scene));
		obs_scene_t *pvs = obs_scene_from_source(preview_scene);
		if (pvs) {
			obs_sceneitem_t *item = obs_scene_add(pvs, globalSource);
			if (item) {
				obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
				obs_sceneitem_set_visible(item, true);
				debugInfo += "  -> Successfully added to Preview Scene!\n";
			} else {
				debugInfo += "  -> FAILED to add to Preview Scene\n";
			}
		} else {
			debugInfo += "  -> FAILED: Preview source is not a valid scene\n";
		}
		obs_source_release(preview_scene);
	} else {
		debugInfo += "\nPreview Scene is NULL!\n";
	}

	// Dump base scenes
	struct obs_frontend_source_list scenes = {0};
	obs_frontend_get_scenes(&scenes);
	debugInfo += QString("\nBase Scenes Count: %1\n").arg(scenes.sources.num);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *base_scene = scenes.sources.array[i];
		inject_into_scene(base_scene, globalSource);
	}
	obs_frontend_source_list_free(&scenes);

	QMessageBox::information(this, "Test Card 0.4.7 DEBUG", debugInfo);
	blog(LOG_INFO, "[TestCardDock] Test card ON");
}

void TestCardDock::deactivateTestCard()
{
	if (globalSource) {
		// 1. Disable the source globally
		obs_source_set_enabled(globalSource, false);
	}

	// 2. Completely obliterate it from all scenes to keep the user's workspace clean
	cleanupStaleSceneItems();

	blog(LOG_INFO, "[TestCardDock] Test card OFF → Exterminated from all scenes");
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void TestCardDock::cleanupStaleSceneItems()
{
	struct obs_frontend_source_list scenes = {0};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (scene) {
			obs_scene_enum_items(scene, remove_stale_items_callback, nullptr);
		}
	}
	obs_frontend_source_list_free(&scenes);

	obs_enum_all_sources(cleanup_all_sources_callback, nullptr);
}

// ---------------------------------------------------------------------------
// Event Handlers
// ---------------------------------------------------------------------------

void TestCardDock::onFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	TestCardDock *dock = static_cast<TestCardDock *>(ptr);

	if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED || event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED) {
		if (!dock->globalSource)
			dock->createGlobalSource();

		if (!dock->isEnabled) {
			dock->cleanupStaleSceneItems();
		}
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (!dock->globalSource)
			dock->createGlobalSource();

		if (dock->isEnabled) {
			dock->activateTestCard();
		} else {
			dock->cleanupStaleSceneItems();
		}
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED || event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED) {
		if (dock->isEnabled) {
			// Ensure it follows the user if they change scenes while ON
			dock->activateTestCard();
		}
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		if (dock->globalSource) {
			obs_source_release(dock->globalSource);
			dock->globalSource = nullptr;
		}
	}
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void TestCardDock::onToggleClicked()
{
	isEnabled = toggleButton->isChecked();

	if (isEnabled)
		activateTestCard();
	else
		deactivateTestCard();

	updateButtonState();
}

void TestCardDock::onSettingsClicked()
{
	if (!globalSource)
		return;
	obs_frontend_open_source_properties(globalSource);
}

void TestCardDock::updateButtonState()
{
	if (isEnabled)
		toggleButton->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
	else
		toggleButton->setStyleSheet("");
}
