// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive.h"

#include "base/flat_map.h"
#include "data/data_document.h"
#include "data/data_media.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "myowngram/message_archive_cleanup.h"
#include "myowngram/message_archive_deletion.h"
#include "myowngram/message_archive_engagement.h"
#include "myowngram/message_archive_index.h"
#include "myowngram/message_archive_markup.h"
#include "myowngram/message_archive_media.h"
#include "myowngram/message_archive_peer.h"
#include "myowngram/message_archive_snapshot.h"
#include "myowngram/message_archive_timeline.h"
#include "myowngram/message_history_settings.h"
#include "storage/storage_account.h"
#include "storage/storage_encryption.h"

#include <crl/crl.h>

namespace MyOwnGram {

// The sequence read and job mutation enter the account FIFO on the main
// thread; error and sequence are accessed only from database callbacks.
// Pending restorations are captured and inspected on the main thread.
// RPCs may retain this handle; queued work owns it through shutdown.
struct MessageArchiveDeleteBound {
	Storage::Cache::Error error;
	uint64 sequence = 0;
	std::vector<std::weak_ptr<MessageArchiveRestore>> pendingRestores;
};

struct MessageArchiveRestore {
	FullMsgId id;
	MessageArchiveStorage::MessageTimelineOrigin origin;
	bool cancelled = false;
	bool displayInline = false;
	rpl::lifetime lifetime;
};

struct MessageArchive::IndexState {
	[[nodiscard]] bool failed() const {
		return invalid || error.type != Storage::Cache::Error::Type::None;
	}

	Storage::Cache::Error error;
	uint64 sequence = 0;
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
	int _visited = 0;
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

struct MessageArchive::LocalDeleteState final
	: public std::enable_shared_from_this<LocalDeleteState> {
	LocalDeleteState(
		base::weak_ptr<MessageArchive> archive,
		MessageArchiveStorage::LocalDeleteJob job);

	void start();

private:
	void applyNext();
	void finish(bool continueQueue);
	[[nodiscard]] bool matches(
		const MessageArchiveStorage::MessageTimelineMetadata &metadata) const;
	void persistPage();
	void processPage(TimelinePage page);
	void readPage();

	base::weak_ptr<MessageArchive> _archive;
	MessageArchiveStorage::LocalDeleteJob _job;
	MsgId _nextBefore;
	std::vector<FullMsgId> _pending;
	size_t _pendingIndex = 0;
	bool _exhausted = false;

};

namespace {

using MessageArchiveStorage::LocalDeleteAction;
using MessageArchiveStorage::LocalDeleteJob;
using MessageArchiveStorage::LocalDeleteScope;
using MessageArchiveStorage::MessagePositionUpdateResult;
using MessageArchiveStorage::MessageSnapshot;
using MessageArchiveStorage::MessageTimeline;
using MessageArchiveStorage::MessageTimelineFlag;
using MessageArchiveStorage::MessageTimelineMetadata;
using MessageArchiveStorage::MessageTimelineOrigin;
using MessageArchiveStorage::ObserveResult;

constexpr auto kLocalDeletePageSize = 100;

struct MutationState {
	bool invalid = false;
};

Storage::MessageArchiveRecordMutation EnqueueLocalDeleteJobMutation(
		QByteArray serialized,
		LocalDeleteJob job,
		bool &invalid) {
	auto jobs = std::vector<LocalDeleteJob>();
	if (!serialized.isEmpty()) {
		auto parsed = MessageArchiveStorage::ParseLocalDeleteJobs(serialized);
		if (!parsed) {
			invalid = true;
			return {};
		}
		jobs = std::move(parsed.value);
	}
	jobs.push_back(job);
	auto value = MessageArchiveStorage::SerializeLocalDeleteJobs(jobs);
	if (!value) {
		invalid = true;
		return {};
	}
	return {
		.value = std::move(*value),
		.action = Storage::MessageArchiveRecordAction::Write,
	};
}

Storage::MessageArchiveRecordMutation PersistLocalDeleteJobMutation(
		QByteArray serialized,
		const LocalDeleteJob &job,
		MsgId nextBefore,
		bool finished,
		bool &invalid) {
	auto parsed = MessageArchiveStorage::ParseLocalDeleteJobs(serialized);
	if (!parsed
		|| parsed.value.empty()
		|| parsed.value.front() != job) {
		invalid = true;
		return {};
	}
	if (finished) {
		parsed.value.erase(parsed.value.begin());
	} else {
		parsed.value.front().before = nextBefore;
	}
	if (parsed.value.empty()) {
		return { .action = Storage::MessageArchiveRecordAction::Remove };
	}
	auto value = MessageArchiveStorage::SerializeLocalDeleteJobs(parsed.value);
	if (!value) {
		invalid = true;
		return {};
	}
	return {
		.value = std::move(*value),
		.action = Storage::MessageArchiveRecordAction::Write,
	};
}

bool ApplyTimelineOrigin(
		MessageTimeline &timeline,
		MessageTimelineOrigin origin,
		uint64 archiveSequence) {
	Expects(archiveSequence != 0);

	if (timeline.origin.from != PeerId()) {
		origin.archiveSequence = timeline.origin.archiveSequence;
		if (timeline.origin != origin) {
			LOG(("Message Archive Error: Timeline origin changed."));
			return false;
		}
	} else {
		origin.archiveSequence = archiveSequence;
		timeline.origin = origin;
	}
	return true;
}

std::optional<QByteArray> ApplyEditedSnapshots(
		QByteArray serialized,
		MessageTimelineOrigin origin,
		uint64 archiveSequence,
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
	if (!ApplyTimelineOrigin(timeline, origin, archiveSequence)) {
		return std::nullopt;
	} else if (!visibleChanged) {
		if (timeline.versions.empty()
			|| !MessageArchiveStorage::SameVisibleContent(
				timeline.versions.back(),
				before)) {
			return std::nullopt;
		}
		const auto result = MessageArchiveStorage::ObserveVersion(
			timeline,
			std::move(after));
		if (result == ObserveResult::Unchanged) {
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
		MessageTimelineOrigin origin,
		uint64 archiveSequence,
		MessageSnapshot snapshot,
		bool historyOnly) {
	auto timeline = MessageTimeline();
	if (!serialized.isEmpty()) {
		auto parsed = MessageArchiveStorage::ParseMessageTimeline(serialized);
		if (!parsed) {
			LOG(("Message Archive Error: Could not parse deleted timeline."));
			return std::nullopt;
		}
		timeline = std::move(parsed.value);
	}
	if (!ApplyTimelineOrigin(timeline, origin, archiveSequence)) {
		return std::nullopt;
	}
	const auto wasDeleted = (timeline.flags & MessageTimelineFlag::Deleted);
	const auto wasHistoryOnly = (timeline.flags
		& MessageTimelineFlag::HistoryOnly);
	const auto observed = MessageArchiveStorage::ObserveVersion(
		timeline,
		std::move(snapshot));
	timeline.flags |= MessageTimelineFlag::Deleted;
	if (historyOnly) {
		timeline.flags |= MessageTimelineFlag::HistoryOnly;
	}
	if (wasDeleted
		&& (!historyOnly || wasHistoryOnly)
		&& observed == ObserveResult::Unchanged) {
		return std::nullopt;
	}
	auto result = MessageArchiveStorage::SerializeMessageTimeline(timeline);
	if (!result) {
		LOG(("Message Archive Error: Could not serialize deleted timeline."));
	}
	return result;
}

std::optional<QByteArray> ApplyHistoryOnly(
		QByteArray serialized,
		bool &invalid) {
	if (serialized.isEmpty()) {
		return std::nullopt;
	}
	auto parsed = MessageArchiveStorage::ParseMessageTimeline(serialized);
	if (!parsed) {
		LOG(("Message Archive Error: Could not parse retained timeline."));
		invalid = true;
		return std::nullopt;
	} else if (parsed.value.flags & MessageTimelineFlag::HistoryOnly) {
		return std::nullopt;
	}
	parsed.value.flags |= MessageTimelineFlag::Deleted;
	parsed.value.flags |= MessageTimelineFlag::HistoryOnly;
	auto result = MessageArchiveStorage::SerializeMessageTimeline(parsed.value);
	if (!result) {
		LOG(("Message Archive Error: Could not serialize retained timeline."));
		invalid = true;
	}
	return result;
}

std::optional<MessageTimelineOrigin> TimelineOrigin(
		not_null<const HistoryItem*> item) {
	const auto from = item->from();
	const auto date = item->date();
	const auto topicRootId = item->history()->isForum()
		? item->topicRootId()
		: MsgId();
	const auto sublistPeer = item->sublistPeerId();
	if (!MessageArchiveStorage::IsValidArchivePeerId(from->id)
		|| date <= 0
		|| (topicRootId && !IsServerMsgId(topicRootId))
		|| (sublistPeer
			&& !MessageArchiveStorage::IsValidArchivePeerId(sublistPeer))
		|| (topicRootId && sublistPeer)) {
		return std::nullopt;
	}
	return MessageTimelineOrigin{
		.from = from->id,
		.date = date,
		.topicRootId = topicRootId,
		.sublistPeer = sublistPeer,
	};
}

bool MatchesLocalDeleteJob(
		const LocalDeleteJob &job,
		const MessageTimelineMetadata &metadata) {
	if (metadata.origin.archiveSequence > job.throughSequence
		|| (job.action == LocalDeleteAction::HistoryOnly
			&& (metadata.flags & MessageTimelineFlag::HistoryOnly))) {
		return false;
	}
	switch (job.scope) {
	case LocalDeleteScope::All:
		return true;
	case LocalDeleteScope::Dates:
		return metadata.origin.date > job.minDate
			&& metadata.origin.date < job.maxDate;
	case LocalDeleteScope::Participant:
		return metadata.origin.from == job.from;
	case LocalDeleteScope::Topic:
		return metadata.origin.topicRootId == job.topicRootId;
	case LocalDeleteScope::Sublist:
		return metadata.origin.sublistPeer == job.sublistPeer;
	}
	Unexpected("Local delete scope in MessageArchive.");
}

#ifdef _DEBUG
void ValidateLocalDeleteJobMutations() {
	static auto checked = false;
	if (checked) {
		return;
	}
	checked = true;

	const auto first = LocalDeleteJob{
		.peer = peerFromUser(UserId(1)),
		.before = MsgId(500),
		.throughSequence = 1,
		.action = LocalDeleteAction::Remove,
		.scope = LocalDeleteScope::All,
	};
	const auto second = LocalDeleteJob{
		.peer = peerFromChannel(ChannelId(2)),
		.from = peerFromUser(UserId(3)),
		.before = MsgId(400),
		.throughSequence = 2,
		.action = LocalDeleteAction::HistoryOnly,
		.scope = LocalDeleteScope::Participant,
	};
	const auto bounded = MessageTimelineMetadata{
		.origin = {
			.from = second.from,
			.date = 1,
			.archiveSequence = 1,
		},
	};
	Assert(MatchesLocalDeleteJob(first, bounded));
	auto afterBound = bounded;
	afterBound.origin.archiveSequence = 2;
	Assert(!MatchesLocalDeleteJob(first, afterBound));
	Assert(MatchesLocalDeleteJob(second, afterBound));
	auto retained = afterBound;
	retained.flags |= MessageTimelineFlag::HistoryOnly;
	Assert(!MatchesLocalDeleteJob(second, retained));

	auto invalid = false;
	auto mutation = EnqueueLocalDeleteJobMutation({}, first, invalid);
	Assert(!invalid
		&& mutation.action == Storage::MessageArchiveRecordAction::Write);
	mutation = EnqueueLocalDeleteJobMutation(
		mutation.value,
		second,
		invalid);
	Assert(!invalid
		&& mutation.action == Storage::MessageArchiveRecordAction::Write);
	auto parsed = MessageArchiveStorage::ParseLocalDeleteJobs(mutation.value);
	Assert(parsed
		&& parsed.value.size() == 2
		&& parsed.value[0] == first
		&& parsed.value[1] == second);
	mutation = PersistLocalDeleteJobMutation(
		mutation.value,
		first,
		MsgId(100),
		false,
		invalid);
	Assert(!invalid
		&& mutation.action == Storage::MessageArchiveRecordAction::Write);
	parsed = MessageArchiveStorage::ParseLocalDeleteJobs(mutation.value);
	Assert(parsed && parsed.value.front().before == MsgId(100));
	auto stale = false;
	PersistLocalDeleteJobMutation(
		mutation.value,
		first,
		MsgId(50),
		false,
		stale);
	Assert(stale);
	mutation = PersistLocalDeleteJobMutation(
		mutation.value,
		parsed.value.front(),
		MsgId(),
		true,
		invalid);
	Assert(!invalid
		&& mutation.action == Storage::MessageArchiveRecordAction::Write);
	parsed = MessageArchiveStorage::ParseLocalDeleteJobs(mutation.value);
	Assert(parsed
		&& parsed.value.size() == 1
		&& parsed.value.front() == second);
	mutation = PersistLocalDeleteJobMutation(
		mutation.value,
		second,
		MsgId(),
		true,
		invalid);
	Assert(!invalid
		&& mutation.action == Storage::MessageArchiveRecordAction::Remove);
}

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
	const auto origin = MessageTimelineOrigin{
		.from = peerFromUser(UserId(123)),
		.date = 456,
		.archiveSequence = 1,
	};
	auto serialized = ApplyEditedSnapshots({}, origin, 1, first, second);
	Assert(serialized.has_value());
	const auto firstEdit = *serialized;
	auto parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 2);
	Assert(parsed.value.origin == origin);

	serialized = ApplyEditedSnapshots(*serialized, origin, 2, second, third);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 3);

	auto refreshed = third;
	refreshed.support = "support";
	serialized = ApplyEditedSnapshots(
		*serialized,
		origin,
		3,
		third,
		refreshed);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 3);
	Assert(parsed.value.versions.back().support == refreshed.support);
	Assert(!ApplyEditedSnapshots({}, origin, 4, third, refreshed));
	Assert(!ApplyEditedSnapshots(firstEdit, origin, 5, third, refreshed));
	auto changedOrigin = origin;
	changedOrigin.date++;
	Assert(!ApplyEditedSnapshots(
		firstEdit,
		changedOrigin,
		6,
		first,
		second));
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
	const auto origin = MessageTimelineOrigin{
		.from = peerFromUser(UserId(123)),
		.date = 456,
		.archiveSequence = 1,
	};
	const auto created = ApplyDeletedSnapshot(
		{},
		origin,
		1,
		snapshot,
		false);
	Assert(created.has_value());
	const auto createdTimeline = MessageArchiveStorage::ParseMessageTimeline(
		*created);
	Assert(createdTimeline && createdTimeline.value.versions.size() == 1);
	Assert(createdTimeline.value.flags
		& MessageArchiveStorage::MessageTimelineFlag::Deleted);

	auto live = MessageTimeline{
		.origin = origin,
		.versions = { snapshot },
	};
	const auto liveSerialized = MessageArchiveStorage::SerializeMessageTimeline(
		live);
	Assert(liveSerialized.has_value());
	auto invalid = false;
	const auto retained = ApplyHistoryOnly(*liveSerialized, invalid);
	Assert(retained.has_value() && !invalid);
	const auto retainedTimeline = MessageArchiveStorage::ParseMessageTimeline(
		*retained);
	Assert(retainedTimeline.value.flags & MessageTimelineFlag::Deleted);
	Assert(retainedTimeline.value.flags & MessageTimelineFlag::HistoryOnly);
	Assert(retainedTimeline.value.versions.size() == 1);
	Assert(!ApplyHistoryOnly(*retained, invalid) && !invalid);
	Assert(!ApplyHistoryOnly(QByteArray("bad"), invalid) && invalid);
	invalid = false;

	auto serialized = ApplyDeletedSnapshot(
		*liveSerialized,
		origin,
		2,
		snapshot,
		false);
	Assert(serialized.has_value());
	auto parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed);
	Assert(parsed.value.flags
		& MessageArchiveStorage::MessageTimelineFlag::Deleted);
	Assert(parsed.value.versions.size() == 1);
	Assert(!ApplyDeletedSnapshot(
		*serialized,
		origin,
		3,
		snapshot,
		false));
	serialized = ApplyDeletedSnapshot(
		*serialized,
		origin,
		4,
		snapshot,
		true);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed.value.flags & MessageTimelineFlag::HistoryOnly);
	Assert(!ApplyDeletedSnapshot(
		*serialized,
		origin,
		5,
		snapshot,
		true));
	Assert(!ApplyHistoryOnly(*serialized, invalid) && !invalid);

	auto refreshed = snapshot;
	refreshed.support = "support-2";
	serialized = ApplyDeletedSnapshot(
		*serialized,
		origin,
		6,
		refreshed,
		false);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 1);
	Assert(parsed.value.versions.back().support == refreshed.support);

	auto changed = refreshed;
	changed.versionDate = 2;
	changed.text = tr::marked(u"changed"_q);
	serialized = ApplyDeletedSnapshot(
		*serialized,
		origin,
		7,
		changed,
		false);
	Assert(serialized.has_value());
	parsed = MessageArchiveStorage::ParseMessageTimeline(*serialized);
	Assert(parsed && parsed.value.versions.size() == 2);
}
#endif // _DEBUG

} // namespace

MessageArchive::LocalDeleteState::LocalDeleteState(
		base::weak_ptr<MessageArchive> archive,
		LocalDeleteJob job)
: _archive(archive)
, _job(job)
, _nextBefore(job.before) {
}

void MessageArchive::LocalDeleteState::start() {
	readPage();
}

void MessageArchive::LocalDeleteState::applyNext() {
	if (_pendingIndex == _pending.size()) {
		persistPage();
		return;
	}
	const auto id = _pending[_pendingIndex];
	if (const auto archive = _archive.get()) {
		const auto self = shared_from_this();
		auto done = [self](Storage::Cache::Error error) {
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Local deletion mutation failed."));
				self->finish(false);
				return;
			}
			++self->_pendingIndex;
			self->applyNext();
		};
		if (_job.action == LocalDeleteAction::Remove) {
			archive->removeMessage(id, std::move(done));
		} else {
			archive->markHistoryOnly(id, std::move(done));
		}
	}
}

void MessageArchive::LocalDeleteState::finish(bool continueQueue) {
	if (const auto archive = _archive.get()) {
		archive->finishLocalDeleteState(this, continueQueue);
	}
}

bool MessageArchive::LocalDeleteState::matches(
		const MessageTimelineMetadata &metadata) const {
	return MatchesLocalDeleteJob(_job, metadata);
}

void MessageArchive::LocalDeleteState::persistPage() {
	if (const auto archive = _archive.get()) {
		const auto self = shared_from_this();
		archive->persistLocalDeleteJob(
			_job,
			_nextBefore,
			_exhausted,
			[self](Storage::Cache::Error error) {
				if (error.type != Storage::Cache::Error::Type::None) {
					LOG(("Message Archive Error: Could not save deletion progress."));
					self->finish(false);
				} else if (self->_exhausted) {
					self->finish(true);
				} else {
					self->_job.before = self->_nextBefore;
					self->readPage();
				}
			});
	}
}

void MessageArchive::LocalDeleteState::processPage(TimelinePage page) {
	if (page.error.type != Storage::Cache::Error::Type::None) {
		LOG(("Message Archive Error: Could not scan local deletions."));
		finish(false);
		return;
	}
	_nextBefore = page.nextBefore;
	_exhausted = page.exhausted;
	_pending.clear();
	_pending.reserve(page.entries.size());
	for (const auto &entry : page.entries) {
		if (matches(entry.metadata)) {
			_pending.push_back(entry.id);
		}
	}
	_pendingIndex = 0;
	applyNext();
}

void MessageArchive::LocalDeleteState::readPage() {
	// ponytail: Filtered cleanup is O(n) per peer because the archive has no
	// secondary date, sender, or thread indexes. The persisted cursor keeps
	// work bounded per page; add a secondary index only if real archive sizes
	// make full peer scans too slow.
	// A cleanup job keeps the last committed exclusive cursor in the database.
	// Every mutation in a page finishes before the cursor advances, so shutdown
	// or an error replays only that page. Removal and HistoryOnly mutations are
	// idempotent, making replay safe without per-message progress records.
	if (const auto archive = _archive.get()) {
		const auto self = shared_from_this();
		archive->readTimelinePage(
			_job.peer,
			_job.before,
			kLocalDeletePageSize,
			[self](TimelinePage page) {
				self->processPage(std::move(page));
			});
	}
}

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
	_result.entries.reserve(limit);
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
		archive->readTimelineMetadata(
			id,
			[self = shared_from_this(), id](
					TimelineMetadataReadResult result) mutable {
				if (result.error.type
						!= Storage::Cache::Error::Type::None) {
					self->finish(std::move(result.error), false);
					return;
				}
				self->_target = uint64(id.msg.bare - 1);
				if (result.exists) {
					Assert(result.value.has_value());
					++self->_visited;
					self->_result.entries.push_back({
						.id = id,
						.metadata = *result.value,
					});
				}
				if (!self->_target) {
					self->finish(Storage::Cache::Error::NoError(), true);
				} else if (self->_visited >= self->_limit) {
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

MessageArchive::MessageArchive(not_null<Data::Session*> owner)
: _owner(owner)
, _account(&owner->session().local()) {
#ifdef _DEBUG
	MessageArchiveStorage::ValidateLocalDeleteJobsFormat();
	ValidateLocalDeleteJobMutations();
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
	const auto weak = base::make_weak(this);
	crl::on_main(weak, [weak] {
		weak->startLocalDeleteJobs();
	});
}

MessageArchive::~MessageArchive() = default;

void MessageArchive::finishLocalDeleteState(
		not_null<LocalDeleteState*> state,
		bool continueQueue) {
	Expects(_localDeleteState.get() == state.get());

	_localDeleteState = nullptr;
	if (continueQueue) {
		startLocalDeleteJobs();
	}
}

void MessageArchive::startLocalDeleteJobs() {
	if (_localDeleteState
		|| _localDeleteJobsReading
		|| (_state == State::Closed
			&& !_account->messageArchiveExists())) {
		return;
	}
	_localDeleteJobsReading = true;
	const auto weak = base::make_weak(this);
	readRecord(
		MessageArchiveStorage::LocalDeleteJobsKey(),
		[weak](ReadResult result) {
			weak->_localDeleteJobsReading = false;
			if (result.error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Could not read deletion jobs."));
				return;
			} else if (result.value.isEmpty()) {
				return;
			}
			auto parsed = MessageArchiveStorage::ParseLocalDeleteJobs(
				result.value);
			if (!parsed) {
				LOG(("Message Archive Error: Could not parse deletion jobs."));
				return;
			}
			Assert(!parsed.value.empty());
			const auto state = std::make_shared<LocalDeleteState>(
				weak,
				parsed.value.front());
			weak->_localDeleteState = state;
			state->start();
		});
}

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

void MessageArchive::readTimeline(
		FullMsgId id,
		TimelineReadDone done) {
	Expects(id.peer != 0);
	Expects(IsServerMsgId(id.msg));
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	if (_state == State::Closed && !_account->messageArchiveExists()) {
		crl::on_main(
			weak,
			[done = std::move(done)]() mutable {
				done(TimelineReadResult{
					.error = Storage::Cache::Error::NoError(),
				});
			});
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	_account->readMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[weak, attempt, done = std::move(done)](
				QByteArray &&value) mutable {
			auto result = TimelineReadResult{
				.error = attempt
					? attempt->error
					: Storage::Cache::Error::NoError(),
				.exists = !value.isEmpty(),
			};
			if (result.error.type == Storage::Cache::Error::Type::None
				&& result.exists) {
				auto parsed = MessageArchiveStorage::ParseMessageTimeline(value);
				result.parseError = parsed.error;
				if (parsed) {
					result.value = std::move(parsed.value);
				}
			}
			crl::on_main(
				weak,
				[
					done = std::move(done),
					result = std::move(result)
				]() mutable {
					done(std::move(result));
				});
		});
}

void MessageArchive::readTimelineMetadata(
		FullMsgId id,
		TimelineMetadataReadDone done) {
	Expects(id.peer != 0);
	Expects(IsServerMsgId(id.msg));
	Expects(done != nullptr);

	const auto weak = base::make_weak(this);
	if (_state == State::Closed && !_account->messageArchiveExists()) {
		crl::on_main(
			weak,
			[done = std::move(done)]() mutable {
				done(TimelineMetadataReadResult{
					.error = Storage::Cache::Error::NoError(),
				});
			});
		return;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	_account->readMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[weak, attempt, done = std::move(done)](
				QByteArray &&value) mutable {
			auto result = TimelineMetadataReadResult{
				.error = attempt
					? attempt->error
					: Storage::Cache::Error::NoError(),
				.exists = !value.isEmpty(),
			};
			if (result.error.type == Storage::Cache::Error::Type::None
				&& result.exists) {
				const auto parsed
					= MessageArchiveStorage::ParseMessageTimelineMetadata(value);
				if (parsed) {
					result.value = parsed.value;
				} else {
					LOG(("Message Archive Error: Could not parse timeline metadata."));
					result.error = Storage::Cache::Error{
						.type = Storage::Cache::Error::Type::IO,
					};
				}
			}
			crl::on_main(
				weak,
				[
					done = std::move(done),
					result = std::move(result)
				]() mutable {
					done(std::move(result));
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

	removeInlineMessage(id);
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

auto MessageArchive::resolveLocalDeleteThrough()
-> std::shared_ptr<MessageArchiveDeleteBound> {
	if (_state == State::Closed && !_account->messageArchiveExists()) {
		return nullptr;
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	const auto through = std::make_shared<MessageArchiveDeleteBound>();
	for (const auto &[id, pending] : _pendingRestores) {
		through->pendingRestores.push_back(pending);
	}
	_account->readMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageArchiveSequenceKey(),
		[attempt, through](QByteArray &&value) {
			if (attempt
				&& attempt->error.type != Storage::Cache::Error::Type::None) {
				through->error = attempt->error;
				LOG(("Message Archive Error: Could not read deletion bound."));
				return;
			}
			const auto parsed = MessageArchiveStorage::ParseMessageArchiveSequence(
				value);
			if (!parsed) {
				LOG(("Message Archive Error: Could not parse deletion bound."));
				through->error = Storage::Cache::Error{
					.type = Storage::Cache::Error::Type::IO,
				};
			} else {
				through->sequence = parsed.value;
			}
		});
	return through;
}

void MessageArchive::enqueueBoundedLocalDeleteJob(
		LocalDeleteJob job,
		const std::shared_ptr<MessageArchiveDeleteBound> &through) {
	Expects(through != nullptr);
	Expects(job.before > MsgId() && job.before <= ServerMaxMsgId);

	// These readers and their timeline writes were queued before the bound
	// read. Their pending origins have no assigned sequence on the main
	// thread yet, but are already inside this action's FIFO boundary.
	// Only these captured tokens are cancelled, not later restoration work.
	for (const auto &weak : through->pendingRestores) {
		if (const auto pending = weak.lock()) {
			if (pending->id.peer == job.peer
				&& MatchesLocalDeleteJob(job, { .origin = pending->origin })) {
				pending->cancelled = true;
			}
		}
	}
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	const auto state = std::make_shared<MutationState>();
	const auto weak = base::make_weak(this);
	_account->mutateMessageArchiveRecord(
		database,
		MessageArchiveStorage::LocalDeleteJobsKey(),
		[attempt, state, job, through](QByteArray &&serialized) mutable {
			if ((attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None)
				|| through->error.type != Storage::Cache::Error::Type::None
				|| !through->sequence) {
				return Storage::MessageArchiveRecordMutation();
			}
			job.throughSequence = through->sequence;
			return EnqueueLocalDeleteJobMutation(
				std::move(serialized),
				job,
				state->invalid);
		},
		[weak, attempt, state, through](Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			} else if (state->invalid
				&& error.type == Storage::Cache::Error::Type::None) {
				error = Storage::Cache::Error{
					.type = Storage::Cache::Error::Type::IO,
				};
			}
			if (through->error.type != Storage::Cache::Error::Type::None) {
				error = through->error;
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Could not queue local deletion."));
			} else if (through->sequence) {
				crl::on_main(weak, [weak] {
					weak->startLocalDeleteJobs();
				});
			}
		});
}

void MessageArchive::persistLocalDeleteJob(
		const LocalDeleteJob &job,
		MsgId nextBefore,
		bool finished,
		WriteDone done) {
	Expects(finished
		|| (nextBefore > MsgId() && nextBefore < job.before));
	Expects(done != nullptr);

	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	const auto state = std::make_shared<MutationState>();
	const auto weak = base::make_weak(this);
	_account->mutateMessageArchiveRecord(
		database,
		MessageArchiveStorage::LocalDeleteJobsKey(),
		[attempt, state, job, nextBefore, finished](
				QByteArray &&serialized) {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				return Storage::MessageArchiveRecordMutation();
			}
			return PersistLocalDeleteJobMutation(
				std::move(serialized),
				job,
				nextBefore,
				finished,
				state->invalid);
		},
		[weak, attempt, state, done = std::move(done)](
				Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			} else if (state->invalid
				&& error.type == Storage::Cache::Error::Type::None) {
				error = Storage::Cache::Error{
					.type = Storage::Cache::Error::Type::IO,
				};
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

void MessageArchive::captureRemoteDeletions(
		const std::vector<not_null<HistoryItem*>> &items) {
	captureDeletions(items, false);
}

void MessageArchive::applyLocalMessageDeletion(
		const std::vector<not_null<HistoryItem*>> &items) {
	applyLocalMessageDeletion(
		items,
		MessageHistory::RemoveSavedHistoryOnDelete());
}

void MessageArchive::applyLocalMessageDeletion(
		const std::vector<not_null<HistoryItem*>> &items,
		bool remove) {
	if (!remove) {
		captureDeletions(items, true);
	}
	auto ids = std::vector<FullMsgId>();
	ids.reserve(items.size());
	for (const auto &item : items) {
		if (item->isRegular() || IsArchivedMsgId(item->id)) {
			ids.push_back(item->fullId());
		}
	}
	applyLocalMessageDeletion(ids, remove);
}

void MessageArchive::applyLocalMessageDeletion(
		const std::vector<FullMsgId> &ids,
		bool remove) {
	for (auto id : ids) {
		if (IsArchivedMsgId(id.msg)) {
			id.msg = OriginalMsgId(id.msg);
		}
		if (remove) {
			removeMessage(id, [](Storage::Cache::Error) {});
		} else {
			markHistoryOnly(id, [](Storage::Cache::Error) {});
		}
	}
}

void MessageArchive::applyLocalHistoryDeletion(
		PeerId peer,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove) {
	Expects(MessageArchiveStorage::IsValidArchivePeerId(peer));

	if (through) {
		enqueueBoundedLocalDeleteJob({
			.peer = peer,
			.before = ServerMaxMsgId,
			.action = remove
				? LocalDeleteAction::Remove
				: LocalDeleteAction::HistoryOnly,
			.scope = LocalDeleteScope::All,
		},
		through);
	}
}

void MessageArchive::applyLocalDateDeletion(
		PeerId peer,
		TimeId minDate,
		TimeId maxDate,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove) {
	Expects(MessageArchiveStorage::IsValidArchivePeerId(peer));
	Expects(minDate < maxDate);

	if (through) {
		enqueueBoundedLocalDeleteJob({
			.peer = peer,
			.before = ServerMaxMsgId,
			.minDate = minDate,
			.maxDate = maxDate,
			.action = remove
				? LocalDeleteAction::Remove
				: LocalDeleteAction::HistoryOnly,
			.scope = LocalDeleteScope::Dates,
		},
		through);
	}
}

void MessageArchive::applyLocalParticipantDeletion(
		PeerId peer,
		PeerId from,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove) {
	Expects(MessageArchiveStorage::IsValidArchivePeerId(peer));
	Expects(MessageArchiveStorage::IsValidArchivePeerId(from));

	if (through) {
		enqueueBoundedLocalDeleteJob({
			.peer = peer,
			.from = from,
			.before = ServerMaxMsgId,
			.action = remove
				? LocalDeleteAction::Remove
				: LocalDeleteAction::HistoryOnly,
			.scope = LocalDeleteScope::Participant,
		},
		through);
	}
}

void MessageArchive::applyLocalTopicDeletion(
		PeerId peer,
		MsgId topicRootId,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove,
		const std::vector<not_null<HistoryItem*>> &items) {
	Expects(MessageArchiveStorage::IsValidArchivePeerId(peer));
	Expects(IsServerMsgId(topicRootId));

	if (through) {
		enqueueBoundedLocalDeleteJob({
			.peer = peer,
			.before = ServerMaxMsgId,
			.topicRootId = topicRootId,
			.action = remove
				? LocalDeleteAction::Remove
				: LocalDeleteAction::HistoryOnly,
			.scope = LocalDeleteScope::Topic,
		},
		through);
	}
	applyLocalMessageDeletion(items, remove);
}

void MessageArchive::applyLocalSublistDeletion(
		PeerId peer,
		PeerId sublistPeer,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove,
		const std::vector<not_null<HistoryItem*>> &items) {
	Expects(MessageArchiveStorage::IsValidArchivePeerId(peer));
	Expects(MessageArchiveStorage::IsValidArchivePeerId(sublistPeer));

	if (through) {
		enqueueBoundedLocalDeleteJob({
			.peer = peer,
			.sublistPeer = sublistPeer,
			.before = ServerMaxMsgId,
			.action = remove
				? LocalDeleteAction::Remove
				: LocalDeleteAction::HistoryOnly,
			.scope = LocalDeleteScope::Sublist,
		},
		through);
	}
	applyLocalMessageDeletion(items, remove);
}

void MessageArchive::observeEdit(
		not_null<const HistoryItem*> item,
		MessageSnapshot before,
		MessageSnapshot after) {
	const auto origin = TimelineOrigin(item);
	if (!origin) {
		return;
	}
	const auto id = item->fullId();
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
			origin = *origin,
			before = std::move(before),
			after = std::move(after)
		](QByteArray &&serialized) mutable {
			if ((attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None)
				|| (index && index->failed())) {
				return std::optional<QByteArray>();
			}
			Assert(!index || index->sequence != 0);
			return ApplyEditedSnapshots(
				std::move(serialized),
				origin,
				index ? index->sequence : uint64(1),
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
		MessageTimelineOrigin origin,
		MessageSnapshot snapshot,
		bool historyOnly) {
	auto &database = databaseForOperation();
	const auto attempt = _openAttempt;
	const auto index = indexMessage(database, attempt, id);
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[
			attempt,
			index,
			origin,
			snapshot = std::move(snapshot),
			historyOnly
		](QByteArray &&serialized) mutable {
			if ((attempt
					&& attempt->error.type
						!= Storage::Cache::Error::Type::None)
				|| (index && index->failed())) {
				return std::optional<QByteArray>();
			}
			Assert(index->sequence != 0);
			return ApplyDeletedSnapshot(
				std::move(serialized),
				origin,
				index->sequence,
				std::move(snapshot),
				historyOnly);
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

void MessageArchive::captureDeletions(
		const std::vector<not_null<HistoryItem*>> &items,
		bool historyOnly) {
	if (!MessageHistory::CaptureEnabled(
			MessageHistory::Capture::DeletedMessages)) {
		return;
	}
	for (const auto &item : items) {
		if (item->out() || item->history()->peer->isSelf()) {
			continue;
		}
		const auto origin = TimelineOrigin(item);
		if (!origin) {
			continue;
		}
		auto snapshot = MessageArchiveStorage::MakeDeletedMessageSnapshot(item);
		if (snapshot) {
			const auto media = historyOnly ? nullptr : item->media();
			if (const auto document = media ? media->document() : nullptr) {
				document->keepDownloadOnMessageRemoval();
			}
			const auto restore = !historyOnly && !item->media();
			auto expected = restore ? *snapshot : MessageSnapshot();
			observeDeletion(
				item->fullId(),
				*origin,
				std::move(*snapshot),
				historyOnly);
			if (restore) {
				restoreDeletedMessage(item, *origin, std::move(expected));
			}
		}
	}
}

void MessageArchive::restoreDeletedMessage(
		not_null<HistoryItem*> item,
		MessageTimelineOrigin origin,
		MessageSnapshot expected) {
	const auto id = item->fullId();
	const auto state = std::make_shared<MessageArchiveRestore>();
	state->id = id;
	state->origin = origin;
	state->displayInline = (item->mainView() != nullptr);
	_pendingRestores[id] = state;
	_owner->historyUnloaded() | rpl::on_next([state = state.get()](
			not_null<History*> history) {
		if (history->peer->id == state->id.peer) {
			state->displayInline = false;
		}
	}, state->lifetime);
	readTimeline(id, [=, this, expected = std::move(expected)](
			TimelineReadResult result) {
		restoreDeletedMessageDone(state, expected, std::move(result));
	});
}

void MessageArchive::restoreDeletedMessageDone(
		const std::shared_ptr<MessageArchiveRestore> &state,
		const MessageSnapshot &expected,
		TimelineReadResult result) {
	const auto id = state->id;
	const auto i = _pendingRestores.find(id);
	if (i == _pendingRestores.end() || i->second != state) {
		return;
	}
	_pendingRestores.erase(i);
	if (state->cancelled
		|| _owner->message(id)
		|| !result.value
		|| result.value->flags != MessageTimelineFlag::Deleted
		|| result.value->versions.empty()) {
		return;
	}
	const auto &saved = result.value->versions.back();
	if (!MessageArchiveStorage::SameVisibleContent(expected, saved)
		|| expected.support != saved.support) {
		return;
	}
	if (const auto history = _owner->historyLoaded(id.peer)) {
		MessageArchiveStorage::RestoreDeletedTextMessage(
			history,
			id.msg,
			saved,
			state->displayInline);
	}
}

void MessageArchive::removeInlineMessage(FullMsgId id) {
	const auto i = _pendingRestores.find(id);
	if (i != _pendingRestores.end()) {
		i->second->cancelled = true;
		_pendingRestores.erase(i);
	}
	crl::on_main(base::make_weak(this), [=, this] {
		if (const auto local = _owner->message(id.peer, ArchivedMsgId(id.msg))) {
			_owner->notifyItemsAboutToBeDestroyed({ local });
			local->destroy();
		}
	});
}

void MessageArchive::markHistoryOnly(FullMsgId id, WriteDone done) {
	Expects(id.peer != 0);
	Expects(IsServerMsgId(id.msg));
	Expects(done != nullptr);

	removeInlineMessage(id);
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
	const auto state = std::make_shared<MutationState>();
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageTimelineKey(id),
		[attempt, state](QByteArray &&serialized) {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				return std::optional<QByteArray>();
			}
			return ApplyHistoryOnly(
				std::move(serialized),
				state->invalid);
		},
		[weak, attempt, state, done = std::move(done)](
				Storage::Cache::Error error) mutable {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			} else if (state->invalid
				&& error.type == Storage::Cache::Error::Type::None) {
				error = Storage::Cache::Error{
					.type = Storage::Cache::Error::Type::IO,
				};
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Message Archive Error: Could not retain local deletion."));
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

std::shared_ptr<MessageArchive::IndexState> MessageArchive::indexMessage(
		Storage::Cache::Database &database,
		const std::shared_ptr<OpenAttempt> &attempt,
		FullMsgId id) {
	const auto state = std::make_shared<IndexState>();
	_account->updateMessageArchiveRecord(
		database,
		MessageArchiveStorage::MessageArchiveSequenceKey(),
		[attempt, state](QByteArray &&serialized) {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				return std::optional<QByteArray>();
			}
			auto updated = MessageArchiveStorage::NextMessageArchiveSequence(
				serialized);
			if (updated.result != MessagePositionUpdateResult::Updated) {
				state->invalid = true;
				return std::optional<QByteArray>();
			}
			const auto parsed = MessageArchiveStorage::ParseMessageArchiveSequence(
				updated.value);
			if (!parsed) {
				state->invalid = true;
				return std::optional<QByteArray>();
			}
			state->sequence = parsed.value;
			return std::make_optional(std::move(updated.value));
		},
		[attempt, state](Storage::Cache::Error error) {
			if (attempt
				&& attempt->error.type
					!= Storage::Cache::Error::Type::None) {
				error = attempt->error;
			}
			if (error.type != Storage::Cache::Error::Type::None) {
				state->error = std::move(error);
				LOG(("Message Archive Error: Could not update archive sequence."));
			} else if (state->invalid) {
				LOG(("Message Archive Error: Could not parse archive sequence."));
			}
		});
	const auto path = MessageArchiveStorage::MessagePositionPath(id);
	// The archive sequence and ancestors are queued before descendants, while
	// the caller queues the timeline last.
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
	if (_state == State::Ready) {
		startLocalDeleteJobs();
	}
}

} // namespace MyOwnGram
