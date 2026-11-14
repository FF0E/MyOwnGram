/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace Storage::Sqlite {
class Database;
} // namespace Storage::Sqlite

namespace Storage {

enum class MessageHistoryKind : qint32 {
	Deleted,
	Edited,
};

struct MessageHistoryEntry {
	qint64 id = 0;
	FullMsgId message;
	MsgId topic;
	PeerId sublist;
	qint64 savedAt = 0;
	MessageHistoryKind kind = MessageHistoryKind::Deleted;
	QByteArray previous;
	QString text;
};

struct MessageHistoryQuery {
	PeerId peer;
	PeerId migratedPeer;
	MsgId message;
	qint64 around = 0;
	std::optional<MessageHistoryKind> kind;
	QString search;
	int limit = 50;
	int limitAfter = 0;
};

struct MessageHistoryRange {
	PeerId peer;
	MsgId topic;
	PeerId sublist;
	MsgId around;
	int limitBefore = 0;
	int limitAfter = 0;
};

struct MessageHistoryPage {
	std::vector<MessageHistoryEntry> entries;
	QString error;
	bool moreBefore = false;
	bool moreAfter = false;
};

namespace MessageHistory {

[[nodiscard]] bool Initialize(Sqlite::Database &database);
[[nodiscard]] QString Add(
	Sqlite::Database &database,
	const std::vector<MessageHistoryEntry> &entries);
[[nodiscard]] MessageHistoryPage Query(
	Sqlite::Database &database,
	const MessageHistoryQuery &query);
[[nodiscard]] bool HasEdits(Sqlite::Database &database, FullMsgId message);
[[nodiscard]] MessageHistoryPage Range(
	Sqlite::Database &database,
	const MessageHistoryRange &range);
[[nodiscard]] QString Clear(
	Sqlite::Database &database,
	PeerId peer,
	MsgId message,
	PeerId migratedPeer,
	std::optional<MessageHistoryKind> kind,
	qint64 recordId);
[[nodiscard]] std::pair<int, QString> Expire(
	Sqlite::Database &database,
	qint64 before);

} // namespace MessageHistory

} // namespace Storage
