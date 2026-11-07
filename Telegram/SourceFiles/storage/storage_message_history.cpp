/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_message_history.h"

#include <QtCore/QFile>
#include <limits>
#include <sqlite3.h>

namespace Storage {
namespace {

constexpr auto kColumns = "id,peer,message,topic,sublist,saved,"
	"kind,previous,updated";

class Statement final {
public:
	Statement(sqlite3 *database, const char *sql, QString &error);
	Statement(const Statement&) = delete;
	~Statement();

	void bind(int index, qint64 value);
	void bind(int index, const QByteArray &value);
	void bindText(int index, const QString &value);
	void reset();
	[[nodiscard]] bool next();
	[[nodiscard]] bool run();
	[[nodiscard]] MessageHistoryEntry entry() const;

private:
	void check(int result);

	sqlite3 *const _database;
	QString &_error;
	sqlite3_stmt *_statement = nullptr;

};

Statement::Statement(sqlite3 *database, const char *sql, QString &error)
: _database(database)
, _error(error) {
	check(sqlite3_prepare_v2(database, sql, -1, &_statement, nullptr));
}

Statement::~Statement() {
	sqlite3_finalize(_statement);
}

void Statement::check(int result) {
	if (result != SQLITE_OK && _error.isEmpty()) {
		_error = QString::fromUtf8(sqlite3_errmsg(_database));
	}
}

void Statement::bind(int index, qint64 value) {
	if (_statement) {
		check(sqlite3_bind_int64(_statement, index, value));
	}
}

void Statement::bind(int index, const QByteArray &value) {
	if (_statement) {
		check(sqlite3_bind_blob64(
			_statement,
			index,
			value.constData(),
			quint64(value.size()),
			SQLITE_STATIC));
	}
}

void Statement::bindText(int index, const QString &value) {
	if (_statement) {
		const auto utf8 = value.toUtf8();
		check(sqlite3_bind_text64(
			_statement,
			index,
			utf8.constData(),
			quint64(utf8.size()),
			SQLITE_TRANSIENT,
			SQLITE_UTF8));
	}
}

void Statement::reset() {
	if (_statement) {
		check(sqlite3_reset(_statement));
		check(sqlite3_clear_bindings(_statement));
	}
}

bool Statement::next() {
	if (!_error.isEmpty()) {
		return false;
	}
	const auto result = sqlite3_step(_statement);
	if (result == SQLITE_ROW) {
		return true;
	} else if (result != SQLITE_DONE) {
		check(result);
	}
	return false;
}

bool Statement::run() {
	if (!_error.isEmpty()) {
		return false;
	}
	const auto result = sqlite3_step(_statement);
	if (result == SQLITE_DONE) {
		return true;
	}
	check(result);
	return false;
}

MessageHistoryEntry Statement::entry() const {
	const auto number = [&](int column) {
		return sqlite3_column_int64(_statement, column);
	};
	const auto blob = [&](int column) {
		return QByteArray(
			static_cast<const char*>(sqlite3_column_blob(_statement, column)),
			sqlite3_column_bytes(_statement, column));
	};
	return {
		.id = number(0),
		.message = FullMsgId(
			DeserializePeerId(quint64(number(1))),
			MsgId(number(2))),
		.topic = MsgId(number(3)),
		.sublist = DeserializePeerId(quint64(number(4))),
		.savedAt = number(5),
		.kind = MessageHistoryKind(number(6)),
		.previous = blob(7),
		.updated = blob(8),
	};
}

} // namespace

MessageHistoryDatabase::MessageHistoryDatabase(
		QString path,
		const QByteArray &key)
: _path(std::move(path)) {
	const auto name = _path.toUtf8();
	const auto opened = sqlite3_open_v2(
		name.constData(),
		&_database,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
		nullptr);
	if (opened != SQLITE_OK) {
		_error = QString::fromUtf8(sqlite3_errmsg(_database));
		close();
		return;
	}
	const auto rawKey = "x'" + key.toHex() + '\'';
	if (sqlite3_key(
			_database,
			rawKey.constData(),
			int(rawKey.size())) != SQLITE_OK) {
		_error = QString::fromUtf8(sqlite3_errmsg(_database));
		close();
		return;
	}
	if (!execute(R"SQL(
		PRAGMA auto_vacuum=INCREMENTAL;
		PRAGMA journal_mode=WAL;
		PRAGMA synchronous=FULL;
		PRAGMA temp_store=MEMORY;
		CREATE TABLE IF NOT EXISTS events (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			peer INTEGER NOT NULL,
			message INTEGER NOT NULL,
			topic INTEGER NOT NULL,
			sublist INTEGER NOT NULL,
			saved INTEGER NOT NULL,
			kind INTEGER NOT NULL,
			previous BLOB NOT NULL,
			updated BLOB NOT NULL,
			text TEXT NOT NULL
		);
		CREATE INDEX IF NOT EXISTS events_chat ON events(peer,id);
		CREATE INDEX IF NOT EXISTS events_message ON events(peer,message,id);
		CREATE INDEX IF NOT EXISTS events_age ON events(saved);
		CREATE UNIQUE INDEX IF NOT EXISTS events_deleted
			ON events(peer,message) WHERE kind=0;
		CREATE VIRTUAL TABLE IF NOT EXISTS events_text USING fts5(
			text,content='events',content_rowid='id',
			tokenize='unicode61 remove_diacritics 2'
		);
		CREATE TRIGGER IF NOT EXISTS events_insert AFTER INSERT ON events BEGIN
			INSERT INTO events_text(rowid,text) VALUES(new.id,new.text);
		END;
		CREATE TRIGGER IF NOT EXISTS events_delete AFTER DELETE ON events BEGIN
			INSERT INTO events_text(events_text,rowid,text)
				VALUES('delete',old.id,old.text);
		END;
	)SQL")) {
		close();
	}
}

MessageHistoryDatabase::~MessageHistoryDatabase() {
	close();
}

void MessageHistoryDatabase::close() {
	if (_database) {
		sqlite3_close_v2(base::take(_database));
	}
}

bool MessageHistoryDatabase::execute(const char *sql) {
	if (sqlite3_exec(_database, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
		return true;
	}
	_error = QString::fromUtf8(sqlite3_errmsg(_database));
	return false;
}

QString MessageHistoryDatabase::add(
		const std::vector<MessageHistoryEntry> &entries) {
	if (!_database) {
		return _error;
	}
	_error.clear();
	if (!execute("BEGIN IMMEDIATE")) {
		return _error;
	}
	auto committed = false;
	const auto rollback = gsl::finally([&] {
		if (!committed) {
			sqlite3_exec(_database, "ROLLBACK", nullptr, nullptr, nullptr);
		}
	});
	auto statement = Statement(_database, R"SQL(
		INSERT INTO events(
			peer,message,topic,sublist,saved,kind,previous,updated,text)
		VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)
		ON CONFLICT(peer,message) WHERE kind=0 DO NOTHING
	)SQL", _error);
	for (const auto &entry : entries) {
		statement.bind(1, qint64(SerializePeerId(entry.message.peer)));
		statement.bind(2, entry.message.msg.bare);
		statement.bind(3, entry.topic.bare);
		statement.bind(4, qint64(SerializePeerId(entry.sublist)));
		statement.bind(5, entry.savedAt);
		statement.bind(6, qint32(entry.kind));
		statement.bind(7, entry.previous);
		statement.bind(8, entry.updated);
		statement.bindText(9, entry.text);
		if (!statement.run()) {
			return _error;
		}
		statement.reset();
	}
	committed = execute("COMMIT");
	return _error;
}

MessageHistoryPage MessageHistoryDatabase::query(
		const MessageHistoryQuery &query) {
	if (!_database) {
		return { .error = _error };
	}
	_error.clear();
	const auto searching = !query.search.isEmpty();
	auto prefix = QByteArray("SELECT ") + kColumns + " FROM events";
	if (searching) {
		prefix += " JOIN events_text ON events_text.rowid=events.id";
	}
	prefix += " WHERE (peer=?1 OR peer=?7)";
	if (query.message) {
		prefix += " AND message=?2";
	}
	if (query.kind) {
		prefix += " AND kind=?4";
	}
	if (searching) {
		prefix += " AND events_text MATCH ?5";
	}
	auto result = MessageHistoryPage();
	for (const auto before : { true, false }) {
		if (!before && (!query.around || !query.limitAfter)) {
			continue;
		}
		const auto sql = prefix + (before
			? " AND id<=?3 ORDER BY id DESC LIMIT ?6"
			: " AND id>?3 ORDER BY id LIMIT ?6");
		auto statement = Statement(_database, sql.constData(), _error);
		statement.bind(1, qint64(SerializePeerId(query.peer)));
		statement.bind(7, query.migratedPeer
			? qint64(SerializePeerId(query.migratedPeer))
			: qint64(SerializePeerId(query.peer)));
		if (query.message) {
			statement.bind(2, query.message.bare);
		}
		statement.bind(3, query.around
			? query.around
			: std::numeric_limits<qint64>::max());
		if (query.kind) {
			statement.bind(4, qint32(*query.kind));
		}
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
			entries.push_back(statement.entry());
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
	result.error = _error;
	return result;
}

MessageHistoryPage MessageHistoryDatabase::range(
		const MessageHistoryRange &range) {
	if (!_database) {
		return { .error = _error };
	}
	_error.clear();
	const auto prefix = QByteArray("SELECT ") + kColumns + R"SQL(
		FROM events WHERE peer=?1 AND kind=0
		AND (?2=0 OR topic=?2 OR message=?2)
		AND (?3=0 OR sublist=?3)
	)SQL";
	auto result = MessageHistoryPage();
	for (const auto before : { true, false }) {
		const auto sql = prefix + (before
			? " AND message<=?4 ORDER BY message DESC LIMIT ?5"
			: " AND message>?4 ORDER BY message LIMIT ?5");
		const auto limit = before ? range.limitBefore + 1 : range.limitAfter;
		auto statement = Statement(_database, sql.constData(), _error);
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
			result.entries.push_back(statement.entry());
		}
		if (before) {
			ranges::reverse(result.entries);
		}
	}
	result.error = _error;
	return result;
}

QString MessageHistoryDatabase::clear(
		PeerId peer,
		MsgId message,
		PeerId migratedPeer) {
	if (!_database) {
		return _error;
	}
	_error.clear();
	auto statement = Statement(_database, message
		? "DELETE FROM events WHERE peer=?1 AND message=?2"
		: "DELETE FROM events WHERE peer=?1 OR peer=?2", _error);
	statement.bind(1, qint64(SerializePeerId(peer)));
	statement.bind(2, message
		? message.bare
		: qint64(SerializePeerId(migratedPeer ? migratedPeer : peer)));
	if (!statement.run() || !execute("PRAGMA incremental_vacuum")) {
		return _error;
	}
	return QString();
}

std::pair<int, QString> MessageHistoryDatabase::expire(qint64 before) {
	if (!_database) {
		return { 0, _error };
	}
	_error.clear();
	auto statement = Statement(
		_database,
		"DELETE FROM events WHERE saved<?1",
		_error);
	statement.bind(1, before);
	if (!statement.run()) {
		return { 0, _error };
	}
	const auto removed = sqlite3_changes(_database);
	if (removed && !execute("PRAGMA incremental_vacuum")) {
		return { removed, _error };
	}
	return { removed, QString() };
}

void MessageHistoryDatabase::erase() {
	close();
	QFile::remove(_path);
	QFile::remove(_path + u"-wal"_q);
	QFile::remove(_path + u"-shm"_q);
}

} // namespace Storage
