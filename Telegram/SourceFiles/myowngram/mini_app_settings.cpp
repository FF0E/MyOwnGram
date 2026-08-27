/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "myowngram/mini_app_settings.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace MyOwnGram::MiniApps {
namespace {

constexpr auto kAskBeforeOpeningKey =
	"myowngram.mini_apps.ask_before_opening";
constexpr auto kOpenInBrowserKey =
	"myowngram.mini_apps.open_in_browser";

rpl::event_stream<OpenMode> ModeUpdates;

constexpr OpenMode ResolveMode(bool ask, bool browser) {
	return ask
		? OpenMode::Ask
		: browser
		? OpenMode::Browser
		: OpenMode::Internal;
}

static_assert(ResolveMode(false, false) == OpenMode::Internal);
static_assert(ResolveMode(true, false) == OpenMode::Ask);
static_assert(ResolveMode(false, true) == OpenMode::Browser);
static_assert(ResolveMode(true, true) == OpenMode::Ask);

} // namespace

OpenMode Mode() {
	const auto &settings = Core::App().settings();
	return ResolveMode(
		settings.readPref<bool>(kAskBeforeOpeningKey, false),
		settings.readPref<bool>(kOpenInBrowserKey, false));
}

rpl::producer<OpenMode> ModeChanges() {
	return ModeUpdates.events();
}

void SetMode(OpenMode mode) {
	const auto ask = (mode == OpenMode::Ask);
	const auto browser = (mode == OpenMode::Browser);
	auto &settings = Core::App().settings();
	if (settings.readPref<bool>(kAskBeforeOpeningKey, false) == ask
		&& settings.readPref<bool>(kOpenInBrowserKey, false) == browser) {
		return;
	}
	settings.writePref<bool>(kAskBeforeOpeningKey, ask);
	settings.writePref<bool>(kOpenInBrowserKey, browser);
	ModeUpdates.fire_copy(mode);
}

} // namespace MyOwnGram::MiniApps
