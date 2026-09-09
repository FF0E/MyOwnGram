// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive.h"

#include "base/flat_map.h"
#include "lang/lang_keys.h"
#include "myowngram/message_archive_deletion.h"
#include "myowngram/message_archive_engagement.h"
#include "myowngram/message_archive_index.h"
#include "myowngram/message_archive_markup.h"
#include "myowngram/message_archive_media.h"
#include "myowngram/message_archive_snapshot.h"
#include "myowngram/message_archive_timeline.h"
#include "storage/storage_account.h"
#include "storage/storage_encryption.h"

#include <crl/crl.h>

namespace MyOwnGram {

struct MessageArchive::IndexState {
	[[nodiscard]] bool failed() const {
		return invalid || error.type != Storage::Cache::Error::Type::None;
	}

	Storage::Cache::Error error;
	bool invalid = false;
};

// Operations enter the database queue right after open() so FIFO is kept.
// The open callback and operation callbacks run on that same queue.
// Each operation keeps this result to report the original open error,
// while the queued operation itself survives MessageArchive destruction.
struct MessageArchive::OpenAttempt {
	Storage::Cache::Error error;
};

// A page is a predecessor walk through prefix bitmaps. Choosing an earlier
// byte fills the remaining suffix with 0xFF; an empty descendant backtracks
// to the nearest earlier parent byte. Index nodes are cached for the page,
// while absent timelines are skipped as crash-safe dangling entries.
struct MessageArchive::PageState final
	: public std::enable_shared_from_this<PageState> {
	PageState(
		base::weak_ptr<MessageArchive> archive,
		PeerId peer,
		MsgId before,
		int limit,
		TimelinePageDone done);

	void start();

private:
	void descend();
	void applyNode(
		const QByteArray &serialized,
		const MessageArchiveStorage::MessagePositionEntry &entry);
	void backtrack();
	void readTimeline();
	void setTargetAt(int level, uint8 bit);
	void finish(Storage::Cache::Error error, bool exhausted);

	base::weak_ptr<MessageArchive> _archive;
	PeerId _peer = 0;
	uint64 _target = 0;
	int _level = 0;
	int _limit = 0;
	TimelinePageDone _done;
	TimelinePage _result;
	base::flat_map<Storage::Cache::Key, QByteArray> _nodes;

};

struct MessageArchive::RemovalState {
	[[nodiscard]] bool failed() const {
		return invalid || error.type != Storage::Cache::Error::Type::None;
	}

	base::weak_ptr<MessageArchive> archive;
	WriteDone done;
	Storage::Cache::Error error;
	bool invalid = false;
	bool stop = false;
};

namespace {

using MessageArchiveStorage::MessagePositionUpdateResult;
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

std::optional<QByteArray> ApplyDeletedSnapshot(
		QByteArray serialized,
		MessageSnapshot snapshot) {
	auto timeline = MessageTimeline();
	if (!serialized.isEmpty()) {
		auto parsed = MessageArchiveStorage::ParseMessageTimeline(serialized);
		if (!parsed) {
			LOG(("Message Archive Error: Could not parse deleted timeline."));
			return std::nullopt;
		}
		timeline = std::move(parsed.value);
	}
	const auto wasDeleted = (timeline.flags.value()
		& uint8(MessageArchiveStorage::MessageTimelineFlag::Deleted)) != 0;
	const auto observed = MessageArchiveStorage::ObserveVersion(
		timeline,
		std::move(snapshot));
	timeline.flags |= MessageArchiveStorage::MessageTimelineFlag::Deleted;
	if (wasDeleted && observed == ObserveResult::Unchanged) {
		return std::nullopt;
	}
	auto result = MessageArchiveStorage::SerializeMessageTimeline(timeline);
	if (!result) {
		LOG(("Message Archive Error: Could not serialize deleted timeline."));
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

void ValidateDeletedTimelineUpdates() {
	static auto checked = false;
	if (checked) {
		return;
	}
	checked = true;

	auto snapshot = MessageSnapshot{
		.versionDate = 1,
		.text = tr::marked(u"current"_q),
		.support = "support-1",
	};
	const auto created = ApplyDeletedSnapshot({}, snapshot);
	Assert(created.has_value());
	const auto createdTimeline = MessageArchiveStorage::ParseMessageTimeline(
		*created);
	Assert(createdTimeline && createdTimeline.value.versions.size() == 1);
	Assert(createdTimeline.value.flags
		& MessageArchiveStorage::MessageTimelineFlag::Deleted);

	auto live = MessageTimeline();
	live.versions.push_back(snapshot);
	const auto liveSerialized = MessageArchiveStorage::SerializeMessageTimeline(
		live);
	Assert(liveSerialized.has_value());
	auto serialized = ApplyDeletedSnapshot(*liveSerialized, snapshot);
	Assert(serialized.has_value());
	auto parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed);
	Assert(parsed.value.flags
		& MessageArchiveStorage::MessageTimelineFlag::Deleted);
	Assert(parsed.value.versions.size() == 1);
	Assert(!ApplyDeletedSnapshot(*serialized, snapshot));

	auto refreshed = snapshot;
	refreshed.support = "support-2";
	serialized = ApplyDeletedSnapshot(*serialized, refreshed);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 1);
	Assert(parsed.value.versions.back().support == refreshed.support);

	auto changed = refreshed;
	changed.versionDate = 2;
	changed.text = tr::marked(u"changed"_q);
	serialized = ApplyDeletedSnapshot(*serialized, changed);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 2);
}
#endif // _DEBUG

} // namespace

MessageArchive::PageState::PageState(
		base::weak_ptr<MessageArchive> archive,
		PeerId peer,
		MsgId before,
		int limit,
		TimelinePageDone done)
: _archive(archive)
, _peer(peer)
, _target(uint64(before.bare - 1))
, _limit(limit)
, _done(std::move(done)) {
	_result.records.reserve(limit);
}

void MessageArchive::PageState::start() {
	if (_target) {
		descend();
	} else {
		finish(Storage::Cache::Error::NoError(), true);
	}
}

void MessageArchive::PageState::descend() {
	Expects(_target != 0);
	Expects(_level >= 0
		&& _level < MessageArchiveStorage::kMessagePositionLevels);

	const auto path = MessageArchiveStorage::MessagePositionPath(FullMsgId(
		_peer,
		MsgId(int64(_target))));
	const auto entry = path[_level];
	const auto i = _nodes.find(entry.key);
	if (i != _nodes.end()) {
		applyNode(i->second, entry);
		return;
	}
	if (const auto archive = _archive.get()) {
		archive->readRecord(
			entry.key,
			[self = shared_from_this(), entry](ReadResult result) mutable {
				if (result.error.type
						!= Storage::Cache::Error::Type::None) {
					self->finish(std::move(result.error), false);
					return;
				}
				const auto i = self->_nodes.emplace(
					entry.key,
					std::move(result.value)).first;
				self->applyNode(i->second, entry);
			});
	}
}

void MessageArchive::PageState::applyNode(
		const QByteArray &serialized,
		const MessageArchiveStorage::MessagePositionEntry &entry) {
	const auto found = MessageArchiveStorage::FindMessagePosition(
		serialized,
		entry.bit);
	if (!found) {
		LOG(("Message Archive Error: Could not parse position index."));
		finish(Storage::Cache::Error{
			.type = Storage::Cache::Error::Type::IO,
		}, false);
		return;
	} else if (!found.found) {
		backtrack();
		return;
	}
	if (found.bit != entry.bit) {
		setTargetAt(_level, found.bit);
	}
	if (++_level
			== MessageArchiveStorage::kMessagePositionLevels) {
		readTimeline();
	} else {
		descend();
	}
}

void MessageArchive::PageState::backtrack() {
	while (_level > 0) {
		const auto level = --_level;
		const auto shift = (MessageArchiveStorage::kMessagePositionLevels
			- level - 1) * 8;
		const auto bit = uint8(_target >> shift);
		if (bit) {
			setTargetAt(level, uint8(bit - 1));
			descend();
			return;
		}
	}
	finish(Storage::Cache::Error::NoError(), true);
}

void MessageArchive::PageState::readTimeline() {
	if (!_target) {
		finish(Storage::Cache::Error::NoError(), true);
		return;
	}
	const auto id = FullMsgId(_peer, MsgId(int64(_target)));
	if (const auto archive = _archive.get()) {
		archive->readRecord(
			MessageArchiveStorage::MessageTimelineKey(id),
			[self = shared_from_this(), id](ReadResult result) mutable {
				if (result.error.type
						!= Storage::Cache::Error::Type::None) {
					self->finish(std::move(result.error), false);
					return;
				}
				self->_target = uint64(id.msg.bare - 1);
				if (!result.value.isEmpty()) {
					self->_result.records.push_back({
						.id = id,
						.value = std::move(result.value),
					});
				}
				if (!self->_target) {
					self->finish(Storage::Cache::Error::NoError(), true);
				} else if (int(self->_result.records.size())
						>= self->_limit) {
					self->finish(Storage::Cache::Error::NoError(), false);
				} else {
					self->_level = 0;
					self->descend();
				}
			});
	}
}

void MessageArchive::PageState::setTargetAt(int level, uint8 bit) {
	const auto shift = (MessageArchiveStorage::kMessagePositionLevels
		- level - 1) * 8;
	Expects(bit < uint8(_target >> shift));
	const auto lower = shift ? ((uint64(1) << shift) - 1) : 0;
	const auto through = shift + 8;
	const auto upper = ~((uint64(1) << through) - 1);
	_target = (_target & upper) | (uint64(bit) << shift) | lower;
}

void MessageArchive::PageState::finish(
		Storage::Cache::Error error,
		bool exhausted) {
	if (!_done) {
		return;
	}
	_result.error = std::move(error);
	_result.exhausted = exhausted;
	if (!exhausted
		&& _result.error.type == Storage::Cache::Error::Type::None) {
		_result.nextBefore = MsgId(int64(_target + 1));
	}
	auto done = base::take(_done);
	done(std::move(_result));
}

MessageArchive::MessageArchive(not_null<Storage::Account*> account)
: _account(account) {
#ifdef _DEBUG
	MessageArchiveStorage::ValidateMessageTimelineFormat();
	MessageArchiveStorage::ValidateDeletedMessageContextFormat();
	MessageArchiveStorage::ValidateDeletedMessageEngagementFormat();
	MessageArchiveStorage::ValidateMessagePositionIndexFormat();
	MessageArchiveStorage::ValidateReplyMarkupFormat();
	MessageArchiveStorage::ValidatePhotoMediaFormat();
	MessageArchiveStorage::ValidateDocumentMediaFormat();
	MessageArchiveStorage::ValidateMessageSnapshotFormat();
	ValidateEditedTimelineUpdates();
	ValidateDeletedTimelineUpdates();
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

void MessageArchive::readTimelinePage(
		PeerId peer,
		MsgId before,
		int limit,
		TimelinePageDone done) {
	Expects(peer != 0);
	Expects(before > MsgId() && before <= ServerMaxMsgId);
	Expects(limit > 0);
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	const auto state = std::make_shared<PageState>(
		weak,
		peer,
		before,
		limit,
		std::move(done));
	crl::on_main(weak, [state] {
		state->start();
	});
}

void MessageArchive::removeMessage(FullMsgId id, WriteDone done) {
	Expects(id.peer != 0);
	Expects(IsServerMsgId(id.msg));
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
	const auto state = std::make_shared<RemovalState>();
	state->archive = weak;
	state->done = std::move(done);
	// The timeline is removed before leaf-to-root index pruning. A crash can
	// leave a dangling bit for readers to skip, but cannot leave an unreachable
	// timeline. Each node mutation atomically writes or removes its record, so
	// a later insertion on the account FIFO cannot be erased by stale cleanup.
	_account->removeMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[attempt, state](Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				state->error = std::move(error);
			}
		});
	const auto path = MessageArchiveStorage::MessagePositionPath(id);
	for (auto level = path.size(); level != 0; --level) {
		const auto entry = path[level - 1];
		const auto last = (level == 1);
		_account->mutateMessageArchiveRecord(
			database,
			entry.key,
			[attempt, state, entry](QByteArray &&serialized) {
				using Action = Storage::MessageArchiveRecordAction;
				using Mutation = Storage::MessageArchiveRecordMutation;
				if ((attempt
						&& attempt->error.type
							!= Storage::Cache::Error::Type::None)
					|| state->failed()
					|| state->stop) {
					return Mutation();
				}
				auto updated = MessageArchiveStorage::RemoveMessagePosition(
					serialized,
					entry);
				if (updated.result == MessagePositionUpdateResult::Invalid) {
					state->invalid = true;
					return Mutation();
				} else if (updated.empty) {
					return Mutation{ .action = Action::Remove };
				}
				state->stop = true;
				return (updated.result
						== MessagePositionUpdateResult::Updated)
					? Mutation{
						.value = std::move(updated.value),
						.action = Action::Write,
					}
					: Mutation();
			},
			[attempt, state, last](Storage::Cache::Error error) mutable {
				if (attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None) {
					error = attempt->error;
				}
				if (error.type != Storage::Cache::Error::Type::None
					&& state->error.type
						== Storage::Cache::Error::Type::None) {
					state->error = std::move(error);
				}
				if (!last) {
					return;
				}
				if (state->invalid
					&& state->error.type
						== Storage::Cache::Error::Type::None) {
					state->error = Storage::Cache::Error{
						.type = Storage::Cache::Error::Type::IO,
					};
				}
				if (state->error.type
						!= Storage::Cache::Error::Type::None) {
					LOG(("Message Archive Error: Could not remove message."));
				}
				crl::on_main(
					state->archive,
					[state]() mutable {
						auto done = base::take(state->done);
						done(std::move(state->error));
					});
			});
	}
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
	const auto index = visibleChanged
		? indexMessage(database, attempt, id)
		: nullptr;
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[
			attempt,
			index,
			before = std::move(before),
			after = std::move(after)
		](QByteArray &&serialized) mutable {
			if ((attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None)
				|| (index && index->failed())) {
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

void MessageArchive::observeDeletion(
		FullMsgId id,
		MessageSnapshot snapshot) {
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	const auto index = indexMessage(database, attempt, id);
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[
			attempt,
			index,
			snapshot = std::move(snapshot)
		](QByteArray &&serialized) mutable {
			if ((attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None)
				|| (index && index->failed())) {
				return std::optional<QByteArray>();
			}
			return ApplyDeletedSnapshot(
				std::move(serialized),
				std::move(snapshot));
		},
		[attempt](Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Could not store deleted timeline."));
			}
		});
}

std::shared_ptr<MessageArchive::IndexState> MessageArchive::indexMessage(
		Storage::Cache::Database &database,
		const std::shared_ptr<OpenAttempt> &attempt,
		FullMsgId id) {
	const auto state = std::make_shared<IndexState>();
	const auto path = MessageArchiveStorage::MessagePositionPath(id);
	// Ancestors are queued before descendants and the timeline is queued last.
	// A crash may leave dangling index bits, which readers can skip when the
	// timeline is absent, but cannot make a newly written timeline unreachable.
	// Index failures also stop the following timeline update from
	// being written.
	// The account-owned FIFO preserves this dispatch order for archive writes.
	// ponytail: Nodes are updated one message at a time to reuse that FIFO.
	// If large deletion batches become measurable, group entries by key and
	// enqueue one bitmap update per node without changing the persisted format.
	for (auto i = path.begin(); i != path.end(); ++i) {
		const auto entry = *i;
		const auto last = (i + 1 == path.end());
		_account->updateMessageArchiveRecord(
			database,
			entry.key,
			[attempt, state, entry](QByteArray &&serialized) {
				if ((attempt
						&& attempt->error.type
							!= Storage::Cache::Error::Type::None)
					|| state->failed()) {
					return std::optional<QByteArray>();
				}
				auto updated = MessageArchiveStorage::AddMessagePosition(
					serialized,
					entry);
				if (updated.result == MessagePositionUpdateResult::Invalid) {
					state->invalid = true;
					return std::optional<QByteArray>();
				}
				return (updated.result == MessagePositionUpdateResult::Updated)
					? std::make_optional(std::move(updated.value))
					: std::optional<QByteArray>();
			},
			[attempt, state, last](Storage::Cache::Error error) {
				if (attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None) {
					error = attempt->error;
				}
				if (error.type != Storage::Cache::Error::Type::None
					&& state->error.type
						== Storage::Cache::Error::Type::None) {
					state->error = std::move(error);
				}
				if (last
					&& state->failed()
					&& (!attempt
						|| attempt->error.type
							== Storage::Cache::Error::Type::None)) {
					LOG(("Message Archive Error: Could not update position index."));
				}
			});
	}
	return state;
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
