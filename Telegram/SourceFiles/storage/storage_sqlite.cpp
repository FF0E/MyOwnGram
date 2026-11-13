/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_sqlite.h"

#include <QtCore/QFile>
#include <sqlite3.h>

namespace Storage::Sqlite {

Database::Database(QString path, const QByteArray &key)
: _path(std::move(path)) {
	const auto name = _path.toUtf8();
	check(sqlite3_open_v2(
		name.constData(),
		&_database,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
		nullptr));
	if (!_error.isEmpty()) {
		close();
		return;
	}
	const auto rawKey = "x'" + key.toHex() + '\'';
	check(sqlite3_key(
		_database,
		rawKey.constData(),
		int(rawKey.size())));
	if (!execute(R"SQL(
		PRAGMA auto_vacuum=INCREMENTAL;
		PRAGMA journal_mode=WAL;
		PRAGMA synchronous=FULL;
		PRAGMA temp_store=MEMORY;
	)SQL")) {
		close();
	}
}

Database::~Database() {
	close();
}

const QString &Database::error() const {
	return _error;
}

void Database::clearError() {
	if (_database) {
		_error.clear();
	}
}

void Database::check(int result) {
	if (result != SQLITE_OK && _error.isEmpty()) {
		_error = QString::fromUtf8(sqlite3_errmsg(_database));
	}
}

bool Database::execute(const char *sql) {
	if (!_database || !_error.isEmpty()) {
		return false;
	}
	check(sqlite3_exec(_database, sql, nullptr, nullptr, nullptr));
	return _error.isEmpty();
}

int Database::changes() const {
	return sqlite3_changes(_database);
}

void Database::close() {
	if (_database) {
		sqlite3_close_v2(base::take(_database));
	}
}

void Database::erase() {
	close();
	QFile::remove(_path);
	QFile::remove(_path + u"-wal"_q);
	QFile::remove(_path + u"-shm"_q);
}

Statement::Statement(Database &database, const char *sql)
: _database(database) {
	if (_database._database && _database.error().isEmpty()) {
		_database.check(sqlite3_prepare_v2(
			_database._database,
			sql,
			-1,
			&_statement,
			nullptr));
	}
}

Statement::~Statement() {
	sqlite3_finalize(_statement);
}

void Statement::bind(int index, qint64 value) {
	if (_statement) {
		_database.check(sqlite3_bind_int64(_statement, index, value));
	}
}

void Statement::bind(int index, const QByteArray &value) {
	if (_statement) {
		_database.check(sqlite3_bind_blob64(
			_statement,
			index,
			value.constData(),
			quint64(value.size()),
			SQLITE_TRANSIENT));
	}
}

void Statement::bindText(int index, const QString &value) {
	if (_statement) {
		const auto utf8 = value.toUtf8();
		_database.check(sqlite3_bind_text64(
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
		_database.check(sqlite3_reset(_statement));
		_database.check(sqlite3_clear_bindings(_statement));
	}
}

bool Statement::next() {
	if (!_statement || !_database.error().isEmpty()) {
		return false;
	}
	const auto result = sqlite3_step(_statement);
	if (result == SQLITE_ROW) {
		return true;
	} else if (result != SQLITE_DONE) {
		_database.check(result);
	}
	return false;
}

bool Statement::run() {
	if (!_statement || !_database.error().isEmpty()) {
		return false;
	}
	const auto result = sqlite3_step(_statement);
	if (result == SQLITE_DONE) {
		return true;
	}
	_database.check(result);
	return false;
}

qint64 Statement::number(int column) const {
	return sqlite3_column_int64(_statement, column);
}

QByteArray Statement::blob(int column) const {
	return QByteArray(
		static_cast<const char*>(sqlite3_column_blob(_statement, column)),
		sqlite3_column_bytes(_statement, column));
}

Transaction::Transaction(Database &database)
: _database(database)
, _active(database.execute("BEGIN IMMEDIATE")) {
}

Transaction::~Transaction() {
	if (_active) {
		sqlite3_exec(_database._database, "ROLLBACK", nullptr, nullptr, nullptr);
	}
}

void Transaction::commit() {
	if (_active && _database.execute("COMMIT")) {
		_active = false;
	}
}

} // namespace Storage::Sqlite
