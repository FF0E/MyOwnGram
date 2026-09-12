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

struct MessagePositionLookup {
	ParseError error = ParseError::Corrupt;
	uint8 bit = 0;
	bool found = false;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

struct MessagePositionUpdate {
	QByteArray value;
	MessagePositionUpdateResult result = MessagePositionUpdateResult::Invalid;
	bool empty = false;
};

struct ParsedMessageArchiveSequence {
	uint64 value = 0;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] MessagePositionLookup FindMessagePosition(
	const QByteArray &serialized,
	uint8 till);
[[nodiscard]] MessagePositionUpdate AddMessagePosition(
	const QByteArray &serialized,
	const MessagePositionEntry &entry);
[[nodiscard]] MessagePositionUpdate RemoveMessagePosition(
	const QByteArray &serialized,
	const MessagePositionEntry &entry);
[[nodiscard]] ParsedMessageArchiveSequence ParseMessageArchiveSequence(
	const QByteArray &serialized);
[[nodiscard]] MessagePositionUpdate NextMessageArchiveSequence(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateMessagePositionIndexFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
