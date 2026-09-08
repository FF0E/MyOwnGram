// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "myowngram/message_archive_timeline.h"

class HistoryItem;

namespace MyOwnGram::MessageArchiveStorage {

enum class MessageMediaType : uint8 {
	None = 0,
	Photo = 1,
	Document = 2,
};

struct MessageMediaVisible {
	MessageMediaType type = MessageMediaType::None;
	bool invertMedia = false;
	QByteArray record;

	friend inline bool operator==(
		const MessageMediaVisible &,
		const MessageMediaVisible &) = default;
};

struct MessageSnapshotSupport {
	QByteArray media;
	QByteArray replyMarkup;
	QByteArray deletion;

	friend inline bool operator==(
		const MessageSnapshotSupport &,
		const MessageSnapshotSupport &) = default;
};

struct ParsedMessageMediaVisible {
	MessageMediaVisible value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

struct ParsedMessageSnapshotSupport {
	MessageSnapshotSupport value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] std::optional<QByteArray> SerializeMessageMediaVisible(
	const MessageMediaVisible &media);
[[nodiscard]] ParsedMessageMediaVisible ParseMessageMediaVisible(
	const QByteArray &serialized);
[[nodiscard]] std::optional<QByteArray> SerializeMessageSnapshotSupport(
	const MessageSnapshotSupport &support);
[[nodiscard]] ParsedMessageSnapshotSupport ParseMessageSnapshotSupport(
	const QByteArray &serialized);
[[nodiscard]] std::optional<MessageSnapshot> MakeMessageSnapshot(
	not_null<const HistoryItem*> item);
[[nodiscard]] std::optional<MessageSnapshot> MakeSavedMediaSnapshot(
	not_null<const HistoryItem*> item);
[[nodiscard]] std::optional<MessageSnapshot> MakeDeletedMessageSnapshot(
	not_null<const HistoryItem*> item);

#ifdef _DEBUG
void ValidateMessageSnapshotFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
