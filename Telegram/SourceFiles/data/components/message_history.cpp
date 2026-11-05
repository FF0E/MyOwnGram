/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/message_history.h"

#include "core/application.h"
#include "core/mime_type.h"
#include "data/data_auto_download.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "storage/serialize_message_history.h"
#include "storage/storage_account.h"

#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <crl/crl_on_main.h>
#include <algorithm>
#include <tuple>

namespace Data {
namespace {

constexpr auto kFlushDelay = crl::time(250);
constexpr auto kExpireInterval = crl::time(60 * 60 * 1000);
constexpr auto kEnabled = "myowngram-message-history-enabled";
constexpr auto kAutoDelete = "myowngram-message-history-auto-delete";
constexpr auto kRetention = "myowngram-message-history-retention";
constexpr auto kDownload = "myowngram-message-history-download";

struct SliceState : base::has_weak_ptr {
	MessagesSlice live;
	base::flat_map<qint64, Serialize::MessageHistory::Item> items;
	Storage::MessageHistoryRange range;
	bool moreBefore = false;
	bool moreAfter = false;
	bool haveLive = false;
	bool haveArchive = false;
	int generation = 0;
};

MessagesSlice MergeDeletedMessages(
		not_null<Main::Session*> session,
		const SliceState &state,
		const Storage::MessageHistoryRange &request,
		FullMsgId anchor) {
	auto result = state.live;
	if (state.items.empty()) {
		return result;
	}
	const auto anchorPeer = anchor.peer ? anchor.peer : request.peer;
	const auto liveCount = result.ids.size();
	for (const auto &[id, item] : state.items) {
		result.ids.push_back(item->fullId());
	}
	const auto position = [&](FullMsgId id) {
		const auto item = session->data().message(id);
		return item ? item->position() : MessagePosition{ id };
	};
	const auto key = [&](FullMsgId id) {
		const auto p = position(id);
		return std::tuple(p.date, !IsServerMsgId(p.fullId.msg), p.fullId);
	};
	const auto middle = result.ids.begin() + liveCount;
	ranges::sort(middle, result.ids.end(), std::less<>(), key);
	std::inplace_merge(
		result.ids.begin(),
		middle,
		result.ids.end(),
		[&](FullMsgId a, FullMsgId b) { return key(a) < key(b); });
	auto around = ranges::find(
		result.ids,
		anchor,
		[&](FullMsgId id) { return position(id).fullId; });
	if (around == result.ids.end()
		&& (!IsServerMsgId(anchor.msg) || anchorPeer != request.peer)) {
		around = ranges::find(result.ids, state.live.nearestToAround);
	}
	if (around == result.ids.end()) {
		around = ranges::find_if(result.ids, [&](FullMsgId id) {
			const auto original = position(id).fullId;
			return (original.peer == anchorPeer)
				&& IsServerMsgId(original.msg)
				&& (original.msg >= state.range.around);
		});
	}
	const auto index = std::min(
		int(around - result.ids.begin()),
		int(result.ids.size()) - 1);
	const auto first = std::max(0, index - request.limitBefore);
	const auto last = std::min(
		int(result.ids.size()),
		index + request.limitAfter + 1);
	result.nearestToAround = result.ids[index];
	result.fullCount.reset();
	result.skippedBefore = (state.live.skippedBefore == 0 && !state.moreBefore)
		? std::make_optional(first)
		: std::nullopt;
	result.skippedAfter = (state.live.skippedAfter == 0 && !state.moreAfter)
		? std::make_optional(int(result.ids.size()) - last)
		: std::nullopt;
	result.ids = std::vector<FullMsgId>(
		result.ids.begin() + first,
		result.ids.begin() + last);
	return result;
}

} // namespace

MessageHistory::MessageHistory(not_null<Main::Session*> session)
: _session(session)
, _enabled(session->local().readPref<bool>(kEnabled))
, _includeAutoDelete(session->local().readPref<bool>(kAutoDelete))
, _retention(session->local().readPref<int>(kRetention))
, _download(Download(session->local().readPref<int>(
	kDownload,
	int(Download::Automatic))))
, _available(QFileInfo::exists(session->local().messageHistoryPath()))
, _flushTimer([=] { flush(); })
, _expireTimer([=] { expire(); }) {
	if (_available && retention()) {
		expire();
	}
}

MessageHistory::~MessageHistory() {
	flush();
}

bool MessageHistory::enabled() const {
	return _enabled.current();
}

bool MessageHistory::available() const {
	return _available || enabled();
}

bool MessageHistory::includeAutoDelete() const {
	return _includeAutoDelete.current();
}

int MessageHistory::retention() const {
	return _retention;
}

MessageHistory::Download MessageHistory::download() const {
	return _download;
}

rpl::producer<bool> MessageHistory::enabledValue() const {
	return _enabled.value();
}

rpl::producer<bool> MessageHistory::includeAutoDeleteValue() const {
	return _includeAutoDelete.value();
}

void MessageHistory::setEnabled(bool enabled) {
	_session->local().writePref<bool>(kEnabled, enabled);
	_enabled = enabled;
}

void MessageHistory::setIncludeAutoDelete(bool include) {
	_session->local().writePref<bool>(kAutoDelete, include);
	_includeAutoDelete = include;
}

void MessageHistory::setRetention(int period) {
	_session->local().writePref<int>(kRetention, period);
	_retention = period;
	_expireTimer.cancel();
	if (_available && period) {
		expire();
	}
}

void MessageHistory::setDownload(Download value) {
	_session->local().writePref<int>(kDownload, int(value));
	_download = value;
}

MessageHistory::Worker &MessageHistory::worker() {
	if (!_worker) {
		_worker = std::make_unique<Worker>(
			_session->local().messageHistoryPath(),
			_session->local().messageHistoryKey());
	}
	return *_worker;
}

bool MessageHistory::captures(not_null<HistoryItem*> item) const {
	return enabled()
		&& item->isRegular()
		&& !item->isService()
		&& (includeAutoDelete() || !item->ttlDestroyAt());
}

Storage::MessageHistoryEntry MessageHistory::snapshot(
		not_null<HistoryItem*> item,
		Storage::MessageHistoryKind kind) const {
	return {
		.message = item->fullId(),
		.topic = item->history()->asForum()
			? item->topicRootId()
			: item->replyToTop(),
		.sublist = item->sublistPeerId(),
		.savedAt = QDateTime::currentSecsSinceEpoch(),
		.kind = kind,
		.previous = Serialize::MessageHistory::Write(item),
		.text = item->clipboardText().expanded,
	};
}

auto MessageHistory::beforeEdit(not_null<HistoryItem*> item)
-> std::optional<Storage::MessageHistoryEntry> {
	if (!captures(item)
		|| _localChanges.contains(item->fullId())
		|| item->isLocalUpdateMedia()) {
		return std::nullopt;
	}
	return snapshot(item, Storage::MessageHistoryKind::Edited);
}

void MessageHistory::afterEdit(
		std::optional<Storage::MessageHistoryEntry> previous,
		not_null<HistoryItem*> item) {
	if (!previous) {
		return;
	}
	previous->updated = Serialize::MessageHistory::Write(item);
	if (previous->previous == previous->updated) {
		return;
	}
	previous->text += '\n' + item->clipboardText().expanded;
	add(std::move(*previous));
}

void MessageHistory::deleted(not_null<HistoryItem*> item) {
	if (!captures(item) || _localChanges.contains(item->fullId())) {
		return;
	}
	add(snapshot(item, Storage::MessageHistoryKind::Deleted));
	recoverMedia(item);
}

std::shared_ptr<void> MessageHistory::localChange(FullMsgId id) {
	++_localChanges[id];
	return std::shared_ptr<void>(nullptr, [weak = base::make_weak(this), id](void*) {
		if (const auto that = weak.get()) {
			const auto i = that->_localChanges.find(id);
			if (i != that->_localChanges.end() && !--i->second) {
				that->_localChanges.erase(i);
			}
		}
	});
}

void MessageHistory::recoverMedia(not_null<HistoryItem*> item) {
	const auto policy = download();
	if (policy == Download::Off) {
		return;
	}
	const auto origin = FileOrigin(item->fullId());
	const auto photo = [&](PhotoData *data) {
		if (data && (policy == Download::Always
			|| AutoDownload::Should(
				_session->settings().autoDownload(),
				item->history()->peer,
				data))) {
			data->load(PhotoSize::Large, origin, LoadFromCloudOrLocal, true);
		}
	};
	const auto document = [&](DocumentData *data) {
		if (!data) {
			return;
		}
		const auto view = data->createMediaView();
		const auto toCache = data->saveToCache();
		const auto allowed = (policy == Download::Always)
			|| (data->status == FileReady
				&& !data->cancelled()
				&& (Core::DetectNameType(data->filename())
					!= Core::NameType::Executable)
				&& AutoDownload::Should(
					_session->settings().autoDownload(),
					item->history()->peer,
					data));
		if (!allowed
			|| view->loaded(true)
			|| data->uploading()
			|| (!toCache
				&& (data->forbidsFileSave()
					|| !Core::App().canSaveFileWithoutAskingForPath()))) {
			return;
		}
		const auto filename = toCache
			? QString()
			: DocumentFileNameForSave(data);
		// WHY: Removing the last live message cancels its document download.
		// Queue recovery after that removal, using only session-owned data.
		crl::on_main(_session, [=] {
			if (view->loaded(true)) {
				return;
			} else if (data->loading()) {
				data->permitLoadFromCloud();
			} else {
				data->save(origin, filename, LoadFromCloudOrLocal, true);
			}
		});
	};
	const auto media = [&](const auto &self, const Media *data) -> void {
		if (!data) {
			return;
		}
		photo(data->photo());
		photo(data->videoCover());
		document(data->document());
		if (const auto game = data->game()) {
			photo(game->photo);
			document(game->document);
		} else if (const auto invoice = data->invoice()) {
			photo(invoice->photo);
			for (const auto &part : invoice->extendedMedia) {
				self(self, part.get());
			}
		}
	};
	media(media, item->media());
	const auto blocks = [&](
			const auto &self,
			const std::vector<Iv::RichPage::Block> &list) -> void {
		for (const auto &block : list) {
			photo(block.photo);
			document(block.document);
			for (const auto &part : block.mediaItems) {
				photo(part.photo);
				document(part.document);
			}
			for (const auto &article : block.relatedArticles) {
				photo(article.photo);
			}
			self(self, block.blocks);
			for (const auto &entry : block.listItems) {
				self(self, entry.blocks);
			}
		}
	};
	const auto page = item->fullRichPage() ? item->fullRichPage() : item->richPage();
	if (page) {
		blocks(blocks, page->blocks);
	}
}

void MessageHistory::add(Storage::MessageHistoryEntry entry) {
	_available = true;
	_pending.push_back(std::move(entry));
	if (!_flushTimer.isActive()) {
		_flushTimer.callOnce(kFlushDelay);
	}
	if (retention() && !_expireTimer.isActive()) {
		_expireTimer.callOnce(kExpireInterval);
	}
}

void MessageHistory::flush() {
	_flushTimer.cancel();
	if (_pending.empty()) {
		return;
	}
	worker().with([
		weak = base::make_weak(this),
		entries = base::take(_pending)
	](Storage::MessageHistoryDatabase &database) {
		const auto error = database.add(entries);
		auto peers = base::flat_set<PeerId>();
		for (const auto &entry : entries) {
			peers.emplace(entry.message.peer);
		}
		crl::on_main(weak, [=] {
			if (!error.isEmpty()) {
				weak->report(error);
			} else {
				for (const auto peer : peers) {
					weak->_changes.fire_copy(peer);
				}
			}
		});
	});
}

void MessageHistory::query(
		Storage::MessageHistoryQuery query,
		Fn<void(Storage::MessageHistoryPage)> done) {
	if (!_available) {
		done({});
		return;
	}
	flush();
	worker().with([
		weak = base::make_weak(this),
		query = std::move(query),
		done = std::move(done)
	](Storage::MessageHistoryDatabase &database) {
		auto result = database.query(query);
		crl::on_main(weak, [done, result = std::move(result)]() mutable {
			done(std::move(result));
		});
	});
}

void MessageHistory::range(
		Storage::MessageHistoryRange range,
		Fn<void(Storage::MessageHistoryPage)> done) {
	if (!_available) {
		done({});
		return;
	}
	flush();
	worker().with([
		weak = base::make_weak(this),
		range,
		done = std::move(done)
	](Storage::MessageHistoryDatabase &database) {
		auto result = database.range(range);
		crl::on_main(weak, [done, result = std::move(result)]() mutable {
			done(std::move(result));
		});
	});
}

void MessageHistory::clear(PeerId peer, MsgId message) {
	if (!_available) {
		return;
	}
	flush();
	worker().with([
		weak = base::make_weak(this),
		peer,
		message
	](Storage::MessageHistoryDatabase &database) {
		const auto error = database.clear(peer, message);
		crl::on_main(weak, [=] { weak->report(error, peer); });
	});
}

void MessageHistory::expire() {
	flush();
	const auto now = QDateTime::currentDateTimeUtc();
	const auto period = retention();
	const auto before = (period < 0)
		? now.addDays(7 * period)
		: now.addMonths(-period);
	worker().with([
		weak = base::make_weak(this),
		before = before.toSecsSinceEpoch()
	](Storage::MessageHistoryDatabase &database) {
		const auto [removed, error] = database.expire(before);
		if (removed || !error.isEmpty()) {
			crl::on_main(weak, [=] {
				if (!error.isEmpty()) {
					weak->report(error);
				}
				if (removed) {
					weak->report({});
				}
			});
		}
	});
	_expireTimer.callOnce(kExpireInterval);
}

void MessageHistory::erase() {
	_pending.clear();
	_flushTimer.cancel();
	_expireTimer.cancel();
	if (_available || _worker) {
		worker().with([](Storage::MessageHistoryDatabase &database) {
			database.erase();
		});
	}
	_available = false;
}

void MessageHistory::report(QString error, PeerId peer) {
	if (error.isEmpty()) {
		_changes.fire_copy(peer);
	} else {
		LOG(("Message history: %1").arg(error));
		_errors.fire({});
	}
}

rpl::producer<PeerId> MessageHistory::changes() const {
	return _changes.events();
}

rpl::producer<> MessageHistory::errors() const {
	return _errors.events();
}

FullMsgId MessageHistory::OriginalId(not_null<const HistoryItem*> item) {
	const auto saved = item->Get<HistoryMessageArchived>();
	return saved ? saved->original : item->fullId();
}

MessagePosition MessageHistory::originalPosition(MessagePosition position) const {
	if (const auto item = _session->data().message(position.fullId)) {
		return item->position();
	}
	return position;
}

rpl::producer<MessagesSlice> MessageHistory::withDeleted(
		Storage::MessageHistoryRange request,
		FullMsgId anchor,
		rpl::producer<MessagesSlice> source) {
	return rpl::make_producer<MessagesSlice>([
		=,
		source = std::move(source)
	](const auto &consumer) mutable {
		auto lifetime = rpl::lifetime();
		const auto state = lifetime.make_state<SliceState>();
		state->range = request;
		const auto emit = [=] {
			if (state->haveLive && state->haveArchive) {
				consumer.put_next(MergeDeletedMessages(
					_session,
					*state,
					request,
					anchor));
			}
		};
		const auto reload = [=] {
			const auto generation = ++state->generation;
			range(state->range, crl::guard(state, [=](Storage::MessageHistoryPage page) {
				if (generation != state->generation) {
					return;
				} else if (!page.error.isEmpty()) {
					report(page.error);
					state->haveArchive = true;
					emit();
					return;
				}
				state->moreBefore = page.moreBefore;
				state->moreAfter = page.moreAfter;
				auto keep = base::flat_set<qint64>();
				for (const auto &entry : page.entries) {
					keep.emplace(entry.id);
					if (state->items.contains(entry.id)) {
						continue;
					}
					auto item = Serialize::MessageHistory::Read(
						_session->data().history(entry.message.peer),
						entry.previous,
						true);
					if (item) {
						const auto saved = item->Get<HistoryMessageArchived>();
						saved->recordId = entry.id;
						saved->deleted = true;
						state->items.emplace(entry.id, std::move(item));
					}
				}
				for (auto i = state->items.begin(); i != state->items.end();) {
					if (keep.contains(i->first)) {
						++i;
					} else {
						i = state->items.erase(i);
					}
				}
				state->haveArchive = true;
				emit();
			}));
		};
		changes() | rpl::filter([=](PeerId peer) {
			return !peer || peer == request.peer;
		}) | rpl::on_next([=] {
			if (state->haveLive) {
				reload();
			}
		}, lifetime);
		std::move(source) | rpl::on_next([=](MessagesSlice slice) {
			const auto first = !state->haveLive;
			state->live = std::move(slice);
			state->haveLive = true;
			emit();
			if (first) {
				if (!IsServerMsgId(state->range.around)) {
					state->range.around = IsServerMsgId(state->live.nearestToAround.msg)
						? state->live.nearestToAround.msg : MsgId(ServerMaxMsgId - 1);
				}
				reload();
			}
		}, lifetime);
		return lifetime;
	});
}

} // namespace Data
