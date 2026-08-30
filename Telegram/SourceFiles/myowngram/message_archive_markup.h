// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "history/history_item_reply_markup.h"
#include "myowngram/message_archive_record.h"

namespace MyOwnGram::MessageArchiveStorage {

struct SerializedReplyMarkup {
	QByteArray visible;
	QByteArray support;
};

struct ParsedReplyMarkup {
	HistoryMessageMarkupData value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] std::optional<SerializedReplyMarkup> SerializeReplyMarkup(
	const HistoryMessageMarkupData *markup);
[[nodiscard]] ParsedReplyMarkup ParseReplyMarkup(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateReplyMarkupFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
