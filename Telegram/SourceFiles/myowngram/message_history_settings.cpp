// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_history_settings.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace MyOwnGram::MessageHistory {
namespace {

constexpr auto kCaptureTypes = std::array{
	Capture::EditHistory,
	Capture::DeletedMessages,
	Capture::ExpiredMedia,
	Capture::DeletedServiceMessages,
};

[[nodiscard]] std::string_view CaptureKey(Capture capture) {
	switch (capture) {
	case Capture::EditHistory:
		return "myowngram.message_history.save_edit_history";
	case Capture::DeletedMessages:
		return "myowngram.message_history.keep_deleted_messages";
	case Capture::ExpiredMedia:
		return "myowngram.message_history.keep_expired_media";
	case Capture::DeletedServiceMessages:
		return "myowngram.message_history.keep_deleted_service_messages";
	}
	Unexpected("Capture in MessageHistory::CaptureKey.");
}

rpl::event_stream<Capture> &CaptureChanges() {
	static auto result = rpl::event_stream<Capture>();
	return result;
}

rpl::event_stream<bool> &RemoveSavedHistoryOnDeleteChanges() {
	static auto result = rpl::event_stream<bool>();
	return result;
}

} // namespace

bool CaptureEnabled(Capture capture) {
	return Core::App().settings().readPref<bool>(CaptureKey(capture), false);
}

void SetCaptureEnabled(Capture capture, bool enabled) {
	if (CaptureEnabled(capture) == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(CaptureKey(capture), enabled);
	CaptureChanges().fire_copy(capture);
}

rpl::producer<bool> CaptureEnabledValue(Capture capture) {
	return rpl::single(CaptureEnabled(capture))
		| rpl::then(
			CaptureChanges().events()
			| rpl::filter([=](Capture changed) {
				return (changed == capture);
			})
			| rpl::map([=](Capture) {
				return CaptureEnabled(capture);
			}))
		| rpl::distinct_until_changed();
}

bool AnyCaptureEnabled() {
	return ranges::any_of(kCaptureTypes, &CaptureEnabled);
}

rpl::producer<bool> AnyCaptureEnabledValue() {
	return rpl::single(AnyCaptureEnabled())
		| rpl::then(
			CaptureChanges().events()
			| rpl::map([](Capture) {
				return AnyCaptureEnabled();
			}))
		| rpl::distinct_until_changed();
}

bool RemoveSavedHistoryOnDelete() {
	return Core::App().settings().readPref<bool>(
		"myowngram.message_history.remove_saved_on_delete",
		true);
}

void SetRemoveSavedHistoryOnDelete(bool enabled) {
	if (RemoveSavedHistoryOnDelete() == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(
		"myowngram.message_history.remove_saved_on_delete",
		enabled);
	RemoveSavedHistoryOnDeleteChanges().fire_copy(enabled);
}

rpl::producer<bool> RemoveSavedHistoryOnDeleteValue() {
	return rpl::single(RemoveSavedHistoryOnDelete())
		| rpl::then(RemoveSavedHistoryOnDeleteChanges().events())
		| rpl::distinct_until_changed();
}

} // namespace MyOwnGram::MessageHistory
