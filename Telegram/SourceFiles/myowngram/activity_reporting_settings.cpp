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

constexpr auto kSendTypingStatusKey
	= "myowngram.activity_reporting.send_typing_status";

} // namespace

bool SendTypingStatus() {
	return Core::App().settings().readPref<bool>(kSendTypingStatusKey, true);
}

void SetSendTypingStatus(bool enabled) {
	Core::App().settings().writePref<bool>(kSendTypingStatusKey, enabled);
}

} // namespace MyOwnGram::ActivityReporting
