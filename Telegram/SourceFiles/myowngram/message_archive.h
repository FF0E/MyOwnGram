// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "storage/cache/storage_cache_database.h"

#include <memory>

struct FullMsgId;

namespace Storage {
class Account;
} // namespace Storage

namespace MyOwnGram::MessageArchiveStorage {
struct MessageSnapshot;
} // namespace MyOwnGram::MessageArchiveStorage

namespace MyOwnGram {

class MessageArchive final : public base::has_weak_ptr {
public:
	struct ReadResult {
		QByteArray value;
		Storage::Cache::Error error;
	};

	using ReadDone = FnMut<void(ReadResult)>;
	using WriteDone = FnMut<void(Storage::Cache::Error)>;

	explicit MessageArchive(not_null<Storage::Account*> account);
	~MessageArchive();

	void readRecord(Storage::Cache::Key key, ReadDone done);
	void writeRecord(
		Storage::Cache::Key key,
		QByteArray value,
		WriteDone done);
	void removeRecord(Storage::Cache::Key key, WriteDone done);
	void observeEdit(
		FullMsgId id,
		MessageArchiveStorage::MessageSnapshot before,
		MessageArchiveStorage::MessageSnapshot after);
	void observeDeletion(
		FullMsgId id,
		MessageArchiveStorage::MessageSnapshot snapshot);

private:
	struct IndexState;
	struct OpenAttempt;

	enum class State {
		Closed,
		Opening,
		Ready,
	};

	[[nodiscard]] Storage::Cache::Database &databaseForOperation();
	[[nodiscard]] std::shared_ptr<IndexState> indexMessage(
		Storage::Cache::Database &database,
		const std::shared_ptr<OpenAttempt> &attempt,
		FullMsgId id);
	void open();
	void openDone(Storage::Cache::Error error);

	const not_null<Storage::Account*> _account;
	Storage::Cache::Database *_database = nullptr;
	std::shared_ptr<OpenAttempt> _openAttempt;
	State _state = State::Closed;

};

} // namespace MyOwnGram
