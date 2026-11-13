/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

struct sqlite3;
struct sqlite3_stmt;

namespace Storage::Sqlite {

class Database final {
public:
	Database(QString path, const QByteArray &key);
	Database(const Database&) = delete;
	~Database();

	[[nodiscard]] const QString &error() const;
	void clearError();
	[[nodiscard]] bool execute(const char *sql);
	[[nodiscard]] int changes() const;
	void close();
	void erase();

private:
	friend class Statement;
	friend class Transaction;

	void check(int result);

	const QString _path;
	sqlite3 *_database = nullptr;
	QString _error;

};

class Statement final {
public:
	Statement(Database &database, const char *sql);
	Statement(const Statement&) = delete;
	~Statement();

	void bind(int index, qint64 value);
	void bind(int index, const QByteArray &value);
	void bindText(int index, const QString &value);
	void reset();
	[[nodiscard]] bool next();
	[[nodiscard]] bool run();
	[[nodiscard]] qint64 number(int column) const;
	[[nodiscard]] QByteArray blob(int column) const;

private:
	Database &_database;
	sqlite3_stmt *_statement = nullptr;

};

class Transaction final {
public:
	explicit Transaction(Database &database);
	Transaction(const Transaction&) = delete;
	~Transaction();

	void commit();

private:
	Database &_database;
	bool _active = false;

};

} // namespace Storage::Sqlite
