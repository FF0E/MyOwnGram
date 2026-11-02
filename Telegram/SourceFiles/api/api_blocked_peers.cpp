/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_blocked_peers.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

namespace Api {
namespace {

constexpr auto kBlockedFirstSlice = 16;
constexpr auto kBlockedPerPage = 40;
constexpr auto kSpoilerCacheKey = "myowngram-blocked-users-cache";

BlockedPeers::Slice TLToSlice(
		const MTPcontacts_Blocked &blocked,
		Data::Session &owner) {
	const auto create = [&](int count, const QVector<MTPPeerBlocked> &list) {
		auto slice = BlockedPeers::Slice();
		slice.total = std::max(count, int(list.size()));
		slice.list.reserve(list.size());
		for (const auto &contact : list) {
			contact.match([&](const MTPDpeerBlocked &data) {
				slice.list.push_back({
					.id = peerFromMTP(data.vpeer_id()),
					.date = data.vdate().v,
				});
			});
		}
		return slice;
	};
	return blocked.match([&](const MTPDcontacts_blockedSlice &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(data.vcount().v, data.vblocked().v);
	}, [&](const MTPDcontacts_blocked &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(0, data.vblocked().v);
	});
}

} // namespace

BlockedPeers::BlockedPeers(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		Core::App().settings().spoilerBlockedUsersValue(
		) | rpl::on_next([=](bool enabled) {
			if (enabled) {
				readSpoilerCache();
				_session->changes().realtimePeerUpdates(
					Data::PeerUpdate::Flag::IsBlocked
				) | rpl::on_next([=](const Data::PeerUpdate &update) {
					updateSpoilerCache(
						update.peer->id,
						update.peer->isBlocked());
				}, _spoilerLifetime);
				loadForSpoilers();
			} else {
				_spoilerLifetime.destroy();
				_api.request(base::take(_spoilerRequestId)).cancel();
				_spoilerLoading = false;
				_spoilerListLoaded = false;
			}
		}, _lifetime);
	});
}

void BlockedPeers::readSpoilerCache() {
	auto stream = QDataStream(
		_session->local().readPref<QByteArray>(kSpoilerCacheKey));
	stream.setVersion(QDataStream::Qt_5_1);
	auto synced = qint32();
	auto ids = QVector<quint64>();
	stream >> synced >> ids;
	if (stream.status() != QDataStream::Ok) {
		return;
	}
	_spoilerLastSync = synced;
	_spoilerIds.clear();
	_spoilerIds.reserve(ids.size());
	for (const auto serialized : ids) {
		_spoilerIds.emplace(DeserializePeerId(serialized));
	}
	_spoilerChanges.fire(PeerId());
}

void BlockedPeers::writeSpoilerCache() {
	auto ids = QVector<quint64>();
	ids.reserve(_spoilerIds.size());
	for (const auto id : _spoilerIds) {
		ids.push_back(SerializePeerId(id));
	}
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(_spoilerLastSync) << ids;
	_session->local().writePref<QByteArray>(kSpoilerCacheKey, std::move(bytes));
}

void BlockedPeers::updateSpoilerCache(PeerId id, bool blocked) {
	if (!Core::App().settings().spoilerBlockedUsers()
		|| _spoilerLoading.current()) {
		return;
	}
	const auto changed = blocked
		? _spoilerIds.emplace(id).second
		: (_spoilerIds.erase(id) != 0);
	if (changed) {
		_spoilerChanges.fire_copy(id);
		if (_spoilerLastSync) {
			writeSpoilerCache();
		}
	}
}

void BlockedPeers::resyncForSpoilers() {
	_spoilerListLoaded = false;
	loadForSpoilers(0, true);
}

bool BlockedPeers::isBlockedForSpoilers(PeerId id) const {
	if (_spoilerIds.contains(id)) {
		return true;
	}
	const auto peer = _session->data().peerLoaded(id);
	return peer && peer->isBlocked();
}

rpl::producer<PeerId> BlockedPeers::spoilerChanges() const {
	return _spoilerChanges.events();
}

rpl::producer<bool> BlockedPeers::spoilerLoadingValue() const {
	return _spoilerLoading.value();
}

void BlockedPeers::loadForSpoilers(int offset, bool full, int count) {
	if (_spoilerRequestId
		|| _spoilerListLoaded
		|| !Core::App().settings().spoilerBlockedUsers()) {
		return;
	}
	if (!offset) {
		full = full || !_spoilerLastSync;
		_spoilerUnseen = full ? _spoilerIds : std::unordered_set<PeerId>();
		_spoilerLoading = true;
	}
	_spoilerRequestId = _api.request(MTPcontacts_GetBlocked(
		MTP_flags(0),
		MTP_int(offset),
		MTP_int(offset ? kBlockedPerPage : kBlockedFirstSlice)
	)).done([=](const MTPcontacts_Blocked &result) {
		_spoilerRequestId = 0;
		applySpoilerSlice(
			TLToSlice(result, _session->data()),
			offset,
			full,
			count);
	}).fail([=] {
		_spoilerRequestId = 0;
		_spoilerLoading = false;
	}).send();
}

void BlockedPeers::applySpoilerSlice(
		const Slice &slice,
		int offset,
		bool full,
		int count) {
	if (!offset) {
		count = full ? slice.total : slice.total - int(_spoilerIds.size());
	}
	for (const auto &entry : slice.list) {
		if (_spoilerIds.emplace(entry.id).second) {
			_spoilerChanges.fire_copy(entry.id);
		}
		_spoilerUnseen.erase(entry.id);
		_session->data().peer(entry.id)->setIsBlocked(true);
	}
	const auto next = offset + int(slice.list.size());
	if (!slice.list.empty() && next < count) {
		loadForSpoilers(next, full, count);
		return;
	}
	for (const auto id : _spoilerUnseen) {
		_spoilerIds.erase(id);
		_spoilerChanges.fire_copy(id);
		if (const auto peer = _session->data().peerLoaded(id)) {
			peer->setIsBlocked(false);
		}
	}
	_spoilerUnseen.clear();
	_spoilerLastSync = base::unixtime::now();
	_spoilerListLoaded = true;
	writeSpoilerCache();
	_spoilerLoading = false;
}

bool BlockedPeers::Slice::Item::operator==(const Item &other) const {
	return (id == other.id) && (date == other.date);
}

bool BlockedPeers::Slice::Item::operator!=(const Item &other) const {
	return !(*this == other);
}

bool BlockedPeers::Slice::operator==(const BlockedPeers::Slice &other) const {
	return (total == other.total) && (list == other.list);
}

bool BlockedPeers::Slice::operator!=(const BlockedPeers::Slice &other) const {
	return !(*this == other);
}

void BlockedPeers::apply(const MTPDupdatePeerBlocked &update) {
	const auto id = peerFromMTP(update.vpeer_id());
	updateSpoilerCache(id, update.is_blocked());
	if (const auto peer = _session->data().peerLoaded(id)) {
		peer->setIsBlocked(update.is_blocked());
	}
}

void BlockedPeers::block(not_null<PeerData*> peer) {
	if (peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		return;
	} else if (blockAlreadySent(peer, true)) {
		return;
	}
	const auto requestId = _api.request(MTPcontacts_Block(
		MTP_flags(0),
		peer->input()
	)).done([=] {
		const auto data = _blockRequests.take(peer);
		peer->setIsBlocked(true);
		updateSpoilerCache(peer->id, true);
		if (_slice) {
			_slice->list.insert(
				_slice->list.begin(),
				{ peer->id, base::unixtime::now() });
			++_slice->total;
			_changes.fire_copy(*_slice);
		}
		if (data) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).fail([=] {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).send();

	_blockRequests.emplace(peer, Request{
		.requestId = requestId,
		.blocking = true,
	});
}

void BlockedPeers::unblock(
		not_null<PeerData*> peer,
		Fn<void(bool success)> done,
		bool force) {
	if (!force && !peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		return;
	} else if (blockAlreadySent(peer, false, done)) {
		return;
	}
	const auto requestId = _api.request(MTPcontacts_Unblock(
		MTP_flags(0),
		peer->input()
	)).done([=] {
		const auto data = _blockRequests.take(peer);
		peer->setIsBlocked(false);
		updateSpoilerCache(peer->id, false);
		if (_slice) {
			auto &list = _slice->list;
			for (auto i = list.begin(); i != list.end(); ++i) {
				if (i->id == peer->id) {
					list.erase(i);
					break;
				}
			}
			if (_slice->total > list.size()) {
				--_slice->total;
			}
			_changes.fire_copy(*_slice);
		}
		if (data) {
			for (const auto &callback : data->callbacks) {
				callback(true);
			}
		}
	}).fail([=] {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).send();
	const auto i = _blockRequests.emplace(peer, Request{
		.requestId = requestId,
		.blocking = false,
	}).first;
	if (done) {
		i->second.callbacks.push_back(std::move(done));
	}
}

bool BlockedPeers::blockAlreadySent(
		not_null<PeerData*> peer,
		bool blocking,
		Fn<void(bool success)> done) {
	const auto i = _blockRequests.find(peer);
	if (i == end(_blockRequests)) {
		return false;
	} else if (i->second.blocking == blocking) {
		if (done) {
			i->second.callbacks.push_back(std::move(done));
		}
		return true;
	}
	const auto callbacks = base::take(i->second.callbacks);
	_blockRequests.erase(i);
	for (const auto &callback : callbacks) {
		callback(false);
	}
	return false;
}

void BlockedPeers::reload() {
	if (_requestId) {
		return;
	}
	request(0, [=](Slice &&slice) {
		if (!_slice || *_slice != slice) {
			_slice = slice;
			_changes.fire(std::move(slice));
		}
	});
}

auto BlockedPeers::slice() -> rpl::producer<BlockedPeers::Slice> {
	if (!_slice) {
		reload();
	}
	return _slice
		? _changes.events_starting_with_copy(*_slice)
		: (_changes.events() | rpl::type_erased);
}

void BlockedPeers::request(int offset, Fn<void(BlockedPeers::Slice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPcontacts_GetBlocked(
		MTP_flags(0),
		MTP_int(offset),
		MTP_int(offset ? kBlockedPerPage : kBlockedFirstSlice)
	)).done([=](const MTPcontacts_Blocked &result) {
		_requestId = 0;
		done(TLToSlice(result, _session->data()));
	}).fail([=] {
		_requestId = 0;
	}).send();
}

} // namespace Api
