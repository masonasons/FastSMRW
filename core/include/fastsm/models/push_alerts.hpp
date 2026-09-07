#pragma once

#include <array>

namespace fastsm {

// Device-wide preferences shared by every push-capable account. Defaults keep
// the alert types enabled by older versions of the mobile clients.
struct PushAlerts {
	bool mention = true;
	bool favourite = true;
	bool reblog = true;
	bool follow = true;
	bool follow_request = true;
	bool poll = true;
	bool status = true;
	bool update = true;
};

struct PushAlertDef {
	const char* key;
	const char* label;
	bool PushAlerts::* enabled;
};

inline constexpr std::array<PushAlertDef, 8> push_alert_catalog = {{
	{"mention", "Mentions", &PushAlerts::mention},
	{"favourite", "Favorites", &PushAlerts::favourite},
	{"reblog", "Boosts", &PushAlerts::reblog},
	{"follow", "Follows", &PushAlerts::follow},
	{"follow_request", "Follow requests", &PushAlerts::follow_request},
	{"poll", "Poll results", &PushAlerts::poll},
	{"status", "Posts from accounts you've enabled notifications for", &PushAlerts::status},
	{"update", "Post edits", &PushAlerts::update},
}};

} // namespace fastsm
