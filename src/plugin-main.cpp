#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include "plugin-main.hpp"
#include "shoutout-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static ShoutoutDock *dock = nullptr;

bool obs_module_load()
{
	blog(LOG_INFO, "[%s] Loading version %s", PLUGIN_NAME, PLUGIN_VERSION);

	auto *mainWindow =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		blog(LOG_ERROR, "[%s] Could not get OBS main window",
		     PLUGIN_NAME);
		return false;
	}

	dock = new ShoutoutDock(mainWindow);

	// Register as a dockable panel — shows up in View > Docks
	obs_frontend_add_dock_by_id("ShoutoutManagerDock",
				    obs_module_text("ShoutoutManager.Title"),
				    dock);

	blog(LOG_INFO, "[%s] Loaded successfully", PLUGIN_NAME);
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "[%s] Unloaded", PLUGIN_NAME);
	// dock lifetime is managed by OBS after add_dock_by_id
}

const char *obs_module_name()
{
	return PLUGIN_NAME;
}

const char *obs_module_description()
{
	return "Twitch shoutout queue manager with 2-minute global cooldown "
	       "and 1-hour per-streamer cooldown.";
}
