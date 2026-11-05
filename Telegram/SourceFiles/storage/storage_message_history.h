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

struct sqlite3;

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
	QByteArray updated;
	QString text;
};

struct MessageHistoryQuery {
	PeerId peer;
	MsgId message;
	qint64 before = 0;
	std::optional<MessageHistoryKind> kind;
	QString search;
	int limit = 50;
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

class MessageHistoryDatabase final {
public:
	MessageHistoryDatabase(QString path, const QByteArray &key);
	MessageHistoryDatabase(const MessageHistoryDatabase&) = delete;
	~MessageHistoryDatabase();

	[[nodiscard]] QString add(const std::vector<MessageHistoryEntry> &entries);
	[[nodiscard]] MessageHistoryPage query(const MessageHistoryQuery &query);
	[[nodiscard]] MessageHistoryPage range(const MessageHistoryRange &range);
	[[nodiscard]] QString clear(PeerId peer, MsgId message = {});
	[[nodiscard]] std::pair<int, QString> expire(qint64 before);
	void erase();

private:
	[[nodiscard]] bool execute(const char *sql);
	void close();

	const QString _path;
	sqlite3 *_database = nullptr;
	QString _error;

};

} // namespace Storage
