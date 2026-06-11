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
	void activateTestCard();
	void deactivateTestCard();
	void cleanupStaleSceneItems();
	void updateButtonState();

	static void onFrontendEvent(enum obs_frontend_event event, void *private_data);

	QPushButton *toggleButton;
	QPushButton *settingsButton;

	obs_source_t *globalSource; // the test_source (__Test_Card_Global__)
	bool isEnabled;
};
