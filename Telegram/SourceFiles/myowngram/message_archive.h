// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "myowngram/message_archive_index.h"
#include "myowngram/message_archive_timeline.h"
#include "rpl/lifetime.h"
#include "rpl/producer.h"
#include "storage/cache/storage_cache_database.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

class History;
class HistoryItem;

namespace Data {
class Session;
struct MessagesRange;
} // namespace Data

namespace Storage {
class Account;
} // namespace Storage

namespace MyOwnGram::MessageArchiveStorage {
struct LocalDeleteJob;
} // namespace MyOwnGram::MessageArchiveStorage

namespace MyOwnGram {

struct MessageArchiveDeleteBound;
struct MessageArchiveRestore;

class MessageArchive final : public base::has_weak_ptr {
public:
	struct ReadResult {
		QByteArray value;
		Storage::Cache::Error error;
	};
	struct TimelineReadResult {
		std::optional<MessageArchiveStorage::MessageTimeline> value;
		Storage::Cache::Error error;
		MessageArchiveStorage::ParseError parseError
			= MessageArchiveStorage::ParseError::None;
		bool exists = false;
	};
	struct TimelinePageEntry {
		FullMsgId id;
		MessageArchiveStorage::MessageTimelineMetadata metadata;
	};
	struct TimelinePage {
		std::vector<TimelinePageEntry> entries;
		MsgId nextCursor;
		Storage::Cache::Error error;
		bool exhausted = false;
	};

	using ReadDone = FnMut<void(ReadResult)>;
	using TimelineReadDone = FnMut<void(TimelineReadResult)>;
	using TimelinePageDone = FnMut<void(TimelinePage)>;
	using WriteDone = FnMut<void(Storage::Cache::Error)>;

	explicit MessageArchive(not_null<Data::Session*> owner);
	~MessageArchive();

	void readRecord(Storage::Cache::Key key, ReadDone done);
	void readTimeline(FullMsgId id, TimelineReadDone done);
	void readTimelinePage(
		PeerId peer,
		MsgId cursor,
		int limit,
		TimelinePageDone done,
		MessageArchiveStorage::MessagePositionDirection direction
			= MessageArchiveStorage::MessagePositionDirection::Older);
	void restoreLoadedMessages(
		not_null<History*> history,
		MessageArchiveStorage::MessagePositionDirection direction,
		int limit,
		bool rangeExpanded = false);
	[[nodiscard]] rpl::producer<bool> restorePreviewMessages(
		not_null<History*> history,
		MsgId cursor,
		MessageArchiveStorage::MessagePositionDirection direction,
		int limit,
		Data::MessagesRange range);
	void interruptLoadedMessages(PeerId peer, bool resume = true);
	void writeRecord(
		Storage::Cache::Key key,
		QByteArray value,
		WriteDone done);
	void removeRecord(Storage::Cache::Key key, WriteDone done);
	[[nodiscard]] auto resolveLocalDeleteThrough()
		-> std::shared_ptr<MessageArchiveDeleteBound>;
	void removeMessage(FullMsgId id, WriteDone done);
	void observeEdit(
		not_null<const HistoryItem*> item,
		MessageArchiveStorage::MessageSnapshot before,
		MessageArchiveStorage::MessageSnapshot after);
	void captureRemoteDeletions(
		const std::vector<not_null<HistoryItem*>> &items);
	void applyLocalMessageDeletion(
		const std::vector<not_null<HistoryItem*>> &items);
	void applyLocalMessageDeletion(
		const std::vector<not_null<HistoryItem*>> &items,
		bool remove);
	void applyLocalMessageDeletion(
		const std::vector<FullMsgId> &ids,
		bool remove);
	void applyLocalHistoryDeletion(
		PeerId peer,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove);
	void applyLocalDateDeletion(
		PeerId peer,
		TimeId minDate,
		TimeId maxDate,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove);
	void applyLocalParticipantDeletion(
		PeerId peer,
		PeerId from,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove);
	void applyLocalTopicDeletion(
		PeerId peer,
		MsgId topicRootId,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove,
		const std::vector<not_null<HistoryItem*>> &items);
	void applyLocalSublistDeletion(
		PeerId peer,
		PeerId sublistPeer,
		const std::shared_ptr<MessageArchiveDeleteBound> &through,
		bool remove,
		const std::vector<not_null<HistoryItem*>> &items);

private:
	struct TimelineMetadataReadResult {
		std::optional<MessageArchiveStorage::MessageTimelineMetadata> value;
		Storage::Cache::Error error;
		bool exists = false;
	};
	using TimelineMetadataReadDone = FnMut<void(TimelineMetadataReadResult)>;

	struct IndexState;
	struct LocalDeleteState;
	struct LoadedHistoryState;
	struct OpenAttempt;
	struct PageState;
	struct RemovalState;

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
	void captureDeletions(
		const std::vector<not_null<HistoryItem*>> &items,
		bool historyOnly);
	void enqueueBoundedLocalDeleteJob(
		MessageArchiveStorage::LocalDeleteJob job,
		const std::shared_ptr<MessageArchiveDeleteBound> &through);
	void finishLocalDeleteState(
		not_null<LocalDeleteState*> state,
		bool continueQueue);
	void markHistoryOnly(FullMsgId id, WriteDone done);
	void removeInlineMessage(FullMsgId id);
	void restoreDeletedMessage(
		not_null<HistoryItem*> item,
		MessageArchiveStorage::MessageTimelineOrigin origin,
		MessageArchiveStorage::MessageSnapshot expected);
	void restoreDeletedMessageDone(
		const std::shared_ptr<MessageArchiveRestore> &state,
		const MessageArchiveStorage::MessageSnapshot &expected,
		TimelineReadResult result);
	void observeDeletion(
		FullMsgId id,
		MessageArchiveStorage::MessageTimelineOrigin origin,
		MessageArchiveStorage::MessageSnapshot snapshot,
		bool historyOnly);
	void open();
	void openDone(Storage::Cache::Error error);
	void readTimelineMetadata(
		FullMsgId id,
		TimelineMetadataReadDone done);
	void persistLocalDeleteJob(
		const MessageArchiveStorage::LocalDeleteJob &job,
		MsgId nextBefore,
		bool finished,
		WriteDone done);
	void startLocalDeleteJobs();

	const not_null<Data::Session*> _owner;
	const not_null<Storage::Account*> _account;
	std::map<FullMsgId, std::shared_ptr<MessageArchiveRestore>> _pendingRestores;
	std::map<
		std::pair<PeerId, MessageArchiveStorage::MessagePositionDirection>,
		std::shared_ptr<LoadedHistoryState>> _loadedHistories;
	std::vector<std::weak_ptr<LoadedHistoryState>> _previewHistories;
	Storage::Cache::Database *_database = nullptr;
	std::shared_ptr<OpenAttempt> _openAttempt;
	std::shared_ptr<LocalDeleteState> _localDeleteState;
	State _state = State::Closed;
	bool _localDeleteJobsReading = false;
	rpl::lifetime _lifetime;

};

} // namespace MyOwnGram
