/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
	[[nodiscard]] bool includeAutoDelete() const;
	[[nodiscard]] int retention() const;
	[[nodiscard]] Download download() const;
	[[nodiscard]] rpl::producer<bool> enabledValue() const;
	[[nodiscard]] rpl::producer<bool> includeAutoDeleteValue() const;
	void setEnabled(bool enabled);
	void setIncludeAutoDelete(bool include);
	void setRetention(int period);
	void setDownload(Download download);

	[[nodiscard]] std::optional<Storage::MessageHistoryEntry> beforeEdit(
		not_null<HistoryItem*> item);
	void afterEdit(
		std::optional<Storage::MessageHistoryEntry> previous,
		not_null<HistoryItem*> item);
	void deleted(not_null<HistoryItem*> item);
	[[nodiscard]] std::shared_ptr<void> localChange(FullMsgId id);

	void query(
		Storage::MessageHistoryQuery query,
		Fn<void(Storage::MessageHistoryPage)> done);
	void range(
		Storage::MessageHistoryRange range,
		Fn<void(Storage::MessageHistoryPage)> done);
	void clear(PeerId peer, MsgId message = {});
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
	using Worker = crl::object_on_queue<Storage::MessageHistoryDatabase>;

	[[nodiscard]] Worker &worker();
	[[nodiscard]] bool captures(not_null<HistoryItem*> item) const;
	[[nodiscard]] Storage::MessageHistoryEntry snapshot(
		not_null<HistoryItem*> item,
		Storage::MessageHistoryKind kind) const;
	void recoverMedia(not_null<HistoryItem*> item);
	void add(Storage::MessageHistoryEntry entry);
	void flush();
	void expire();
	void report(QString error, PeerId peer = {});

	const not_null<Main::Session*> _session;
	rpl::variable<bool> _enabled;
	rpl::variable<bool> _includeAutoDelete;
	int _retention = 0;
	Download _download = Download::Automatic;
	bool _available = false;
	std::unique_ptr<Worker> _worker;
	std::vector<Storage::MessageHistoryEntry> _pending;
	base::flat_map<FullMsgId, int> _localChanges;
	base::Timer _flushTimer;
	base::Timer _expireTimer;
	rpl::event_stream<PeerId> _changes;
	rpl::event_stream<> _errors;

};

} // namespace Data
