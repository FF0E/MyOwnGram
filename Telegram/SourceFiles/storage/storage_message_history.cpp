/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_message_history.h"

#include "storage/storage_sqlite.h"

#include <limits>

namespace Storage::MessageHistory {
namespace {

constexpr auto kColumns = "id,peer,message,topic,sublist,saved,"
	"kind,previous";

MessageHistoryEntry ReadEntry(const Sqlite::Statement &statement) {
	return {
		.id = statement.number(0),
		.message = FullMsgId(
			DeserializePeerId(quint64(statement.number(1))),
			MsgId(statement.number(2))),
		.topic = MsgId(statement.number(3)),
		.sublist = DeserializePeerId(quint64(statement.number(4))),
		.savedAt = statement.number(5),
		.kind = MessageHistoryKind(statement.number(6)),
		.previous = statement.blob(7),
	};
}

} // namespace

bool Initialize(Sqlite::Database &database) {
	return database.execute(R"SQL(
		CREATE TABLE IF NOT EXISTS message_history (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			peer INTEGER NOT NULL,
			message INTEGER NOT NULL,
			topic INTEGER NOT NULL,
			sublist INTEGER NOT NULL,
			saved INTEGER NOT NULL,
			kind INTEGER NOT NULL,
			previous BLOB NOT NULL,
			text TEXT NOT NULL
		);
		CREATE INDEX IF NOT EXISTS message_history_deleted_history
			ON message_history(peer,id) WHERE kind=0;
		CREATE INDEX IF NOT EXISTS message_history_message
			ON message_history(peer,message,id);
		CREATE INDEX IF NOT EXISTS message_history_age ON message_history(saved);
		CREATE UNIQUE INDEX IF NOT EXISTS message_history_deleted
			ON message_history(peer,message) WHERE kind=0;
		CREATE VIRTUAL TABLE IF NOT EXISTS message_history_text USING fts5(
			text,content='message_history',content_rowid='id',
			tokenize='unicode61 remove_diacritics 2'
		);
		CREATE TRIGGER IF NOT EXISTS message_history_insert
		AFTER INSERT ON message_history BEGIN
			INSERT INTO message_history_text(rowid,text) VALUES(new.id,new.text);
		END;
		CREATE TRIGGER IF NOT EXISTS message_history_delete
		AFTER DELETE ON message_history BEGIN
			INSERT INTO message_history_text(message_history_text,rowid,text)
				VALUES('delete',old.id,old.text);
		END;
	)SQL");
}

QString Add(
		Sqlite::Database &database,
		const std::vector<MessageHistoryEntry> &entries) {
	database.clearError();
	auto transaction = Sqlite::Transaction(database);
	auto statement = Sqlite::Statement(database, R"SQL(
		INSERT INTO message_history(
			peer,message,topic,sublist,saved,kind,previous,text)
		VALUES(?1,?2,?3,?4,?5,?6,?7,?8)
		ON CONFLICT(peer,message) WHERE kind=0 DO NOTHING
	)SQL");
	for (const auto &entry : entries) {
		statement.bind(1, qint64(SerializePeerId(entry.message.peer)));
		statement.bind(2, entry.message.msg.bare);
		statement.bind(3, entry.topic.bare);
		statement.bind(4, qint64(SerializePeerId(entry.sublist)));
		statement.bind(5, entry.savedAt);
		statement.bind(6, qint32(entry.kind));
		statement.bind(7, entry.previous);
		statement.bindText(8, entry.text);
		if (!statement.run()) {
			return database.error();
		}
		statement.reset();
	}
	transaction.commit();
	return database.error();
}

MessageHistoryPage Query(
		Sqlite::Database &database,
		const MessageHistoryQuery &query) {
	database.clearError();
	const auto searching = !query.search.isEmpty();
	const auto migrated = query.migratedPeer && query.migratedPeer != query.peer;
	auto prefix = QByteArray("SELECT ") + kColumns + " FROM message_history";
	if (searching) {
		prefix += " JOIN message_history_text"
			" ON message_history_text.rowid=message_history.id";
	}
	prefix += migrated ? " WHERE (peer=?1 OR peer=?7)" : " WHERE peer=?1";
	if (query.message) {
		prefix += " AND message=?2";
	}
	prefix += " AND kind=?4";
	if (searching) {
		prefix += " AND message_history_text MATCH ?5";
	}
	auto result = MessageHistoryPage();
	for (const auto before : { true, false }) {
		if (!before && (!query.around || !query.limitAfter)) {
			continue;
		}
		const auto sql = prefix + (before
			? " AND id<=?3 ORDER BY id DESC LIMIT ?6"
			: " AND id>?3 ORDER BY id LIMIT ?6");
		auto statement = Sqlite::Statement(database, sql.constData());
		statement.bind(1, qint64(SerializePeerId(query.peer)));
		if (migrated) {
			statement.bind(7, qint64(SerializePeerId(query.migratedPeer)));
		}
		if (query.message) {
			statement.bind(2, query.message.bare);
		}
		statement.bind(3, query.around
			? query.around
			: std::numeric_limits<qint64>::max());
		statement.bind(4, qint32(query.kind));
		if (searching) {
			auto text = query.search;
			statement.bindText(5, '"' + text.replace('"', u"\"\""_q) + u"\"*"_q);
		}
		const auto limit = before ? query.limit : query.limitAfter;
		statement.bind(6, limit + 1);
		auto entries = std::vector<MessageHistoryEntry>();
		while (statement.next()) {
			if (int(entries.size()) == limit) {
				(before ? result.moreBefore : result.moreAfter) = true;
				break;
			}
			entries.push_back(ReadEntry(statement));
		}
		if (before) {
			result.entries = std::move(entries);
		} else {
			ranges::reverse(entries);
			result.entries.insert(
				result.entries.begin(),
				std::make_move_iterator(entries.begin()),
				std::make_move_iterator(entries.end()));
		}
	}
	result.error = database.error();
	return result;
}

bool HasEdits(Sqlite::Database &database, FullMsgId message) {
	database.clearError();
	auto statement = Sqlite::Statement(
		database,
		"SELECT 1 FROM message_history"
		" WHERE peer=?1 AND message=?2 AND kind=1 LIMIT 1");
	statement.bind(1, qint64(SerializePeerId(message.peer)));
	statement.bind(2, message.msg.bare);
	return statement.next();
}

MessageHistoryPage Range(
		Sqlite::Database &database,
		const MessageHistoryRange &range) {
	database.clearError();
	const auto prefix = QByteArray("SELECT ") + kColumns + R"SQL(
		FROM message_history WHERE peer=?1 AND kind=0
		AND (?2=0 OR topic=?2 OR message=?2)
		AND (?3=0 OR sublist=?3)
	)SQL";
	auto result = MessageHistoryPage();
	for (const auto before : { true, false }) {
		const auto sql = prefix + (before
			? " AND message<=?4 ORDER BY message DESC LIMIT ?5"
			: " AND message>?4 ORDER BY message LIMIT ?5");
		const auto limit = before ? range.limitBefore + 1 : range.limitAfter;
		auto statement = Sqlite::Statement(database, sql.constData());
		statement.bind(1, qint64(SerializePeerId(range.peer)));
		statement.bind(2, range.topic.bare);
		statement.bind(3, range.sublist
			? qint64(SerializePeerId(range.sublist))
			: 0);
		statement.bind(4, range.around.bare);
		statement.bind(5, limit + 1);
		auto count = 0;
		while (statement.next()) {
			if (++count > limit) {
				(before ? result.moreBefore : result.moreAfter) = true;
				break;
			}
			result.entries.push_back(ReadEntry(statement));
		}
		if (before) {
			ranges::reverse(result.entries);
		}
	}
	result.error = database.error();
	return result;
}

QString Clear(
		Sqlite::Database &database,
		PeerId peer,
		MsgId message,
		PeerId migratedPeer,
		std::optional<MessageHistoryKind> kind,
		qint64 recordId) {
	database.clearError();
	auto statement = Sqlite::Statement(
		database,
		"DELETE FROM message_history WHERE (peer=?1 OR peer=?2)"
		" AND (?3=0 OR message=?3) AND (?4=-1 OR kind=?4)"
		" AND (?5=0 OR id=?5)");
	statement.bind(1, qint64(SerializePeerId(peer)));
	statement.bind(2, qint64(SerializePeerId(migratedPeer ? migratedPeer : peer)));
	statement.bind(3, message.bare);
	statement.bind(4, kind ? qint64(*kind) : -1);
	statement.bind(5, recordId);
	if (!statement.run() || !database.execute("PRAGMA incremental_vacuum")) {
		return database.error();
	}
	return QString();
}

std::pair<int, QString> Expire(Sqlite::Database &database, qint64 before) {
	database.clearError();
	auto statement = Sqlite::Statement(
		database,
		"DELETE FROM message_history WHERE saved<?1");
	statement.bind(1, before);
	if (!statement.run()) {
		return { 0, database.error() };
	}
	const auto removed = database.changes();
	if (removed && !database.execute("PRAGMA incremental_vacuum")) {
		return { removed, database.error() };
	}
	return { removed, QString() };
}

} // namespace Storage::MessageHistory
