// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive.h"

#include "myowngram/message_archive_markup.h"
#include "myowngram/message_archive_media.h"
#include "myowngram/message_archive_timeline.h"
#include "storage/storage_account.h"
#include "storage/storage_encryption.h"

#include <crl/crl.h>

namespace MyOwnGram {

// Operations enter the database queue right after open() so FIFO is kept.
// The open callback and operation callbacks run on that same queue.
// Each operation keeps this result to report the original open error,
// while the queued operation itself survives MessageArchive destruction.
struct MessageArchive::OpenAttempt {
	Storage::Cache::Error error;
};

MessageArchive::MessageArchive(not_null<Storage::Account*> account)
: _account(account) {
#ifdef _DEBUG
	MessageArchiveStorage::ValidateMessageTimelineFormat();
	MessageArchiveStorage::ValidateReplyMarkupFormat();
	MessageArchiveStorage::ValidatePhotoMediaFormat();
#endif // _DEBUG
}

MessageArchive::~MessageArchive() = default;

void MessageArchive::readRecord(
		Storage::Cache::Key key,
		ReadDone done) {
	Expects(key.valid());
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	if (_state == State::Closed && !_account->messageArchiveExists()) {
		crl::on_main(
			weak,
			[done = std::move(done)]() mutable {
				done(ReadResult{
					.value = {},
					.error = Storage::Cache::Error::NoError(),
				});
			});
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	database.get(
		key,
		[weak, attempt, done = std::move(done)](
				QByteArray &&value) mutable {
			auto error = attempt
				? attempt->error
				: Storage::Cache::Error::NoError();
			crl::on_main(
				weak,
				[
					done = std::move(done),
					error = std::move(error),
					value = std::move(value)
				]() mutable {
					done(ReadResult{
						.value = std::move(value),
						.error = std::move(error),
					});
				});
		});
}

void MessageArchive::writeRecord(
		Storage::Cache::Key key,
		QByteArray value,
		WriteDone done) {
	Expects(key.valid());
	Expects(!value.isEmpty());
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	if (value.size() > Storage::kMessageArchiveMaxRecordSize) {
		crl::on_main(
			weak,
			[done = std::move(done)]() mutable {
				done(Storage::Cache::Error{
					.type = Storage::Cache::Error::Type::IO,
				});
			});
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	database.put(
		key,
		std::move(value),
		[weak, attempt, done = std::move(done)](
				Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type != Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			crl::on_main(
				weak,
				[
					done = std::move(done),
					error = std::move(error)
				]() mutable {
					done(std::move(error));
				});
		});
}

void MessageArchive::removeRecord(
		Storage::Cache::Key key,
		WriteDone done) {
	Expects(key.valid());
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	if (_state == State::Closed && !_account->messageArchiveExists()) {
		crl::on_main(
			weak,
			[done = std::move(done)]() mutable {
				done(Storage::Cache::Error::NoError());
			});
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	database.remove(
		key,
		[weak, attempt, done = std::move(done)](
				Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type != Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			crl::on_main(
				weak,
				[
					done = std::move(done),
					error = std::move(error)
				]() mutable {
					done(std::move(error));
				});
		});
}

Storage::Cache::Database &MessageArchive::databaseForOperation() {
	if (_state == State::Closed) {
		open();
	}
	Assert(_database != nullptr);
	return *_database;
}

void MessageArchive::open() {
	Expects(_state == State::Closed);

	_state = State::Opening;
	_database = &_account->messageArchiveDatabase();
	_openAttempt = std::make_shared<OpenAttempt>();
	const auto attempt = _openAttempt;
	const auto weak = base::make_weak(this);
	_database->open(
		_account->messageArchiveKey(),
		[weak, attempt](Storage::Cache::Error error) mutable {
			attempt->error = error;
			crl::on_main(
				weak,
				[weak, error = std::move(error)]() mutable {
					weak->openDone(std::move(error));
				});
		});
}

void MessageArchive::openDone(Storage::Cache::Error error) {
	Expects(_state == State::Opening);

	_state = (error.type == Storage::Cache::Error::Type::None)
		? State::Ready
		: State::Closed;
	_openAttempt = nullptr;
}

} // namespace MyOwnGram
