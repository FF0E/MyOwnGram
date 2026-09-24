// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

namespace MyOwnGram::MessageHistory {

enum class Capture {
	EditHistory,
	DeletedMessages,
	ExpiredMedia,
	DeletedServiceMessages,
};

[[nodiscard]] bool CaptureEnabled(Capture capture);
void SetCaptureEnabled(Capture capture, bool enabled);
[[nodiscard]] rpl::producer<bool> CaptureEnabledValue(Capture capture);

[[nodiscard]] bool AnyCaptureEnabled();
[[nodiscard]] rpl::producer<bool> AnyCaptureEnabledValue();

[[nodiscard]] bool TranslucentDeletedMessages();
void SetTranslucentDeletedMessages(bool enabled);
[[nodiscard]] rpl::producer<bool> TranslucentDeletedMessagesValue();

[[nodiscard]] bool RemoveSavedHistoryOnDelete();
void SetRemoveSavedHistoryOnDelete(bool enabled);
[[nodiscard]] rpl::producer<bool> RemoveSavedHistoryOnDeleteValue();

} // namespace MyOwnGram::MessageHistory
