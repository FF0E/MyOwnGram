/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "myowngram/activity_reporting_settings.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace MyOwnGram::ActivityReporting {
namespace {

struct Setting {
	std::string_view key;
	rpl::event_stream<bool> changes;
};

Setting SendTypingStatusState = {
	.key = "myowngram.activity_reporting.send_typing_status",
};
Setting SendReadMetricsState = {
	.key = "myowngram.activity_reporting.send_read_metrics",
};
Setting SendMusicListenReportsState = {
	.key = "myowngram.activity_reporting.send_music_listen_reports",
};

bool Read(const Setting &setting) {
	return Core::App().settings().readPref<bool>(setting.key, true);
}

rpl::producer<bool> Changes(Setting &setting) {
	return setting.changes.events();
}

void Write(Setting &setting, bool enabled) {
	if (Read(setting) == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(setting.key, enabled);
	setting.changes.fire_copy(enabled);
}

} // namespace

bool SendTypingStatus() {
	return Read(SendTypingStatusState);
}

rpl::producer<bool> SendTypingStatusChanges() {
	return Changes(SendTypingStatusState);
}

void SetSendTypingStatus(bool enabled) {
	Write(SendTypingStatusState, enabled);
}

bool SendReadMetrics() {
	return Read(SendReadMetricsState);
}

rpl::producer<bool> SendReadMetricsChanges() {
	return Changes(SendReadMetricsState);
}

void SetSendReadMetrics(bool enabled) {
	Write(SendReadMetricsState, enabled);
}

bool SendMusicListenReports() {
	return Read(SendMusicListenReportsState);
}

rpl::producer<bool> SendMusicListenReportsChanges() {
	return Changes(SendMusicListenReportsState);
}

void SetSendMusicListenReports(bool enabled) {
	Write(SendMusicListenReportsState, enabled);
}

} // namespace MyOwnGram::ActivityReporting
