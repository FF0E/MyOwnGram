// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "data/data_peer_id.h"
#include "data/data_types.h"
#include "myowngram/message_archive_record.h"

#include <optional>

class HistoryItem;

namespace MyOwnGram::MessageArchiveStorage {

struct DeletedMessageContext {
	PeerId from;
	TimeId date = 0;
	TimeId editDate = 0;
	uint64 groupedId = 0;
	bool post = false;
	bool authorHidden = false;
	bool noForwards = false;
	bool hideEdited = false;

	friend inline bool operator==(
		const DeletedMessageContext &,
		const DeletedMessageContext &) = default;
};

struct ParsedDeletedMessageContext {
	DeletedMessageContext value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] std::optional<QByteArray> SerializeDeletedMessageContext(
	const DeletedMessageContext &context);
[[nodiscard]] std::optional<QByteArray> SerializeDeletedMessageContext(
	not_null<const HistoryItem*> item);
[[nodiscard]] ParsedDeletedMessageContext ParseDeletedMessageContext(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateDeletedMessageContextFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
