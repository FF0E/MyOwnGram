// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "myowngram/message_archive_record.h"

namespace MyOwnGram::MessageArchiveStorage {

enum class MessagePositionUpdateResult : uint8 {
	Invalid,
	Unchanged,
	Updated,
};

struct MessagePositionUpdate {
	QByteArray value;
	MessagePositionUpdateResult result = MessagePositionUpdateResult::Invalid;
};

[[nodiscard]] MessagePositionUpdate AddMessagePosition(
	const QByteArray &serialized,
	const MessagePositionEntry &entry);

#ifdef _DEBUG
void ValidateMessagePositionIndexFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
