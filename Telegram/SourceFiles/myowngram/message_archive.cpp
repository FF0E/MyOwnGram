// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive.h"

#include "lang/lang_keys.h"
#include "myowngram/message_archive_deletion.h"
#include "myowngram/message_archive_engagement.h"
#include "myowngram/message_archive_markup.h"
#include "myowngram/message_archive_media.h"
#include "myowngram/message_archive_snapshot.h"
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

namespace {

using MessageArchiveStorage::MessageSnapshot;
using MessageArchiveStorage::MessageTimeline;
using MessageArchiveStorage::ObserveResult;

std::optional<QByteArray> ApplyEditedSnapshots(
		QByteArray serialized,
		MessageSnapshot before,
		MessageSnapshot after) {
	const auto visibleChanged = !MessageArchiveStorage::SameVisibleContent(
		before,
		after);
	if (serialized.isEmpty() && !visibleChanged) {
		return std::nullopt;
	}
	auto timeline = MessageTimeline();
	if (!serialized.isEmpty()) {
		auto parsed = MessageArchiveStorage::ParseMessageTimeline(serialized);
		if (!parsed) {
			LOG(("Message Archive Error: Could not parse edited timeline."));
			return std::nullopt;
		}
		timeline = std::move(parsed.value);
	}
	if (!visibleChanged) {
		if (timeline.versions.empty()
			|| !MessageArchiveStorage::SameVisibleContent(
				timeline.versions.back(),
				before)
			|| MessageArchiveStorage::ObserveVersion(
				timeline,
				std::move(after)) == ObserveResult::Unchanged) {
			return std::nullopt;
		}
	} else {
		const auto beforeResult = MessageArchiveStorage::ObserveVersion(
			timeline,
			std::move(before));
		const auto afterResult = MessageArchiveStorage::ObserveVersion(
			timeline,
			std::move(after));
		if (beforeResult == ObserveResult::Unchanged
			&& afterResult == ObserveResult::Unchanged) {
			return std::nullopt;
		}
	}
	auto result = MessageArchiveStorage::SerializeMessageTimeline(timeline);
	if (!result) {
		LOG(("Message Archive Error: Could not serialize edited timeline."));
	}
	return result;
}

#ifdef _DEBUG
void ValidateEditedTimelineUpdates() {
	static auto checked = false;
	if (checked) {
		return;
	}
	checked = true;

	auto first = MessageSnapshot{
		.versionDate = 1,
		.text = tr::marked(u"first"_q),
	};
	auto second = MessageSnapshot{
		.versionDate = 2,
		.text = tr::marked(u"second"_q),
	};
	auto third = MessageSnapshot{
		.versionDate = 3,
		.text = tr::marked(u"third"_q),
	};
	auto serialized = ApplyEditedSnapshots({}, first, second);
	Assert(serialized.has_value());
	const auto firstEdit = *serialized;
	auto parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 2);

	serialized = ApplyEditedSnapshots(*serialized, second, third);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 3);

	auto refreshed = third;
	refreshed.support = "support";
	serialized = ApplyEditedSnapshots(*serialized, third, refreshed);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 3);
	Assert(parsed.value.versions.back().support == refreshed.support);
	Assert(!ApplyEditedSnapshots({}, third, refreshed));
	Assert(!ApplyEditedSnapshots(firstEdit, third, refreshed));
}
#endif // _DEBUG

} // namespace

MessageArchive::MessageArchive(not_null<Storage::Account*> account)
: _account(account) {
#ifdef _DEBUG
	MessageArchiveStorage::ValidateMessageTimelineFormat();
	MessageArchiveStorage::ValidateDeletedMessageContextFormat();
	MessageArchiveStorage::ValidateDeletedMessageEngagementFormat();
	MessageArchiveStorage::ValidateReplyMarkupFormat();
	MessageArchiveStorage::ValidatePhotoMediaFormat();
	MessageArchiveStorage::ValidateDocumentMediaFormat();
	MessageArchiveStorage::ValidateMessageSnapshotFormat();
	ValidateEditedTimelineUpdates();
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
	_account->readMessageArchiveRecord(
		database,
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
	_account->writeMessageArchiveRecord(
		database,
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
	_account->removeMessageArchiveRecord(
		database,
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

void MessageArchive::observeEdit(
		FullMsgId id,
		MessageSnapshot before,
		MessageSnapshot after) {
	const auto visibleChanged = !MessageArchiveStorage::SameVisibleContent(
		before,
		after);
	if (!visibleChanged && before.support == after.support) {
		return;
	} else if (!visibleChanged
		&& _state == State::Closed
		&& !_account->messageArchiveExists()) {
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[
			attempt,
			before = std::move(before),
			after = std::move(after)
		](QByteArray &&serialized) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				return std::optional<QByteArray>();
			}
			return ApplyEditedSnapshots(
				std::move(serialized),
				std::move(before),
				std::move(after));
		},
		[attempt](Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Could not store edited timeline."));
			}
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
