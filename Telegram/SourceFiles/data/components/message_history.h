/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include <crl/crl_object_on_queue.h>
#include "data/data_messages.h"
#include <rpl/variable.h>
#include "storage/storage_message_history.h"

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class MessageHistory final : public base::has_weak_ptr {
public:
	enum class Download : qint32 { Off, Automatic, Always };

	explicit MessageHistory(not_null<Main::Session*> session);
	~MessageHistory();

	[[nodiscard]] bool enabled() const;
	[[nodiscard]] bool available() const;
	[[nodiscard]] bool saveEdits() const;
	[[nodiscard]] bool saveDeleted() const;
	[[nodiscard]] bool includeAutoDelete() const;
	[[nodiscard]] int retention() const;
	[[nodiscard]] Download download() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] rpl::producer<bool> saveEditsValue() const;
	[[nodiscard]] rpl::producer<bool> saveDeletedValue() const;
	[[nodiscard]] rpl::producer<bool> includeAutoDeleteValue() const;
	[[nodiscard]] rpl::producer<int> retentionValue() const;
	[[nodiscard]] rpl::producer<Download> downloadValue() const;
	void setSaveEdits(bool enabled);
	void setSaveDeleted(bool enabled);
	void setIncludeAutoDelete(bool include);
	void setRetention(int period);
	void setDownload(Download download);

	[[nodiscard]] Fn<void()> changingItem(FullMsgId id);
	[[nodiscard]] Fn<void()> changingItems(
		const base::flat_map<not_null<HistoryItem*>, int> &items);
	void deleted(not_null<HistoryItem*> item);
	[[nodiscard]] std::shared_ptr<void> localChange(FullMsgId id);

	void query(
		Storage::MessageHistoryQuery query,
		Fn<void(Storage::MessageHistoryPage)> done);
	void hasEdits(FullMsgId message, Fn<void(bool)> done);
	void range(
		Storage::MessageHistoryRange range,
		Fn<void(Storage::MessageHistoryPage)> done);
	void clear(
		PeerId peer,
		MsgId message = {},
		std::optional<Storage::MessageHistoryKind> kind = {},
		qint64 recordId = 0);
	void erase();
	[[nodiscard]] rpl::producer<PeerId> changes() const;
	[[nodiscard]] rpl::producer<> errors() const;

	[[nodiscard]] rpl::producer<MessagesSlice> withDeleted(
		Storage::MessageHistoryRange range,
		FullMsgId anchor,
		rpl::producer<MessagesSlice> source);
	[[nodiscard]] MessagePosition originalPosition(MessagePosition position) const;
	[[nodiscard]] static FullMsgId OriginalId(not_null<const HistoryItem*> item);

private:
	struct Edit {
		Storage::MessageHistoryEntry entry;
		QByteArray content;
	};

	[[nodiscard]] std::optional<Edit> beforeEdit(not_null<HistoryItem*> item);
	void afterEdit(std::optional<Edit> previous, not_null<HistoryItem*> item);

	struct PendingEntry {
		Storage::MessageHistoryEntry entry;
		std::shared_ptr<HistoryItem> item;
	};

	struct CachedItem {
		MsgId topic;
		PeerId sublist;
		std::weak_ptr<HistoryItem> item;
	};

	using Worker = crl::object_on_queue<Storage::MessageHistoryDatabase>;

	[[nodiscard]] Worker &worker();
	[[nodiscard]] bool captures(not_null<HistoryItem*> item) const;
	[[nodiscard]] Storage::MessageHistoryEntry snapshot(
		not_null<HistoryItem*> item,
		Storage::MessageHistoryKind kind) const;
	[[nodiscard]] std::shared_ptr<HistoryItem> inlineItem(
		const Storage::MessageHistoryEntry &entry);
	void recoverMedia(not_null<HistoryItem*> item);
	void add(
		Storage::MessageHistoryEntry entry,
		std::shared_ptr<HistoryItem> item = nullptr);
	void flush();
	void expire();
	void report(QString error, PeerId peer = {});

	const not_null<Main::Session*> _session;
	rpl::variable<bool> _saveEdits;
	rpl::variable<bool> _saveDeleted;
	rpl::variable<bool> _includeAutoDelete;
	rpl::variable<int> _retention = 0;
	rpl::variable<Download> _download = Download::Automatic;
	bool _available = false;
	bool _saveFailed = false;
	std::unique_ptr<Worker> _worker;
	base::flat_map<qint64, PendingEntry> _pending;
	base::flat_map<FullMsgId, CachedItem> _inlineItems;
	qint64 _nextPendingId = 0;
	qint64 _savingTill = 0;
	base::flat_map<FullMsgId, int> _localChanges;
	base::flat_set<FullMsgId> _editing;
	base::Timer _flushTimer;
	base::Timer _expireTimer;
	rpl::event_stream<PeerId> _changes;
	rpl::event_stream<std::shared_ptr<HistoryItem>> _deleted;
	rpl::event_stream<> _errors;

};

} // namespace Data
