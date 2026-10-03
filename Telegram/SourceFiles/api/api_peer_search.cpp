/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_peer_search.h"

#include "api/api_single_message_search.h"
#include "apiwrap.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_session.h"
#include "dialogs/ui/chat_search_in.h" // IsHashOrCashtagSearchQuery
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kMinSponsoredQueryLength = 4;

} // namespace

PeerSearch::PeerSearch(not_null<Main::Session*> session, Type type)
: _session(session)
, _type(type)
, _adSettings(Core::App().settings().adSettings(Core::AdPlacement::Search)) {
	if (_type == Type::WithSponsored) {
		Core::App().settings().adSettingsValue(
			Core::AdPlacement::Search
		) | rpl::skip(1) | rpl::on_next([=](Core::AdSettings settings) {
			applyAdSettings(settings);
		}, _lifetime);
	}
}

PeerSearch::~PeerSearch() {
	_lifetime.destroy();
	clear();
}

rpl::producer<PeerSearchResult> PeerSearch::changes() const {
	return _changes.events();
}

void PeerSearch::applyAdSettings(Core::AdSettings settings) {
	const auto previous = std::exchange(_adSettings, settings);
	const auto retry = settings.get
		&& (!previous.get || (settings.show && !previous.show));
	for (auto &[query, cache] : _cache) {
		if (!settings.show && cache.sponsoredRequested) {
			cache.result.sponsored.clear();
			cache.sponsoredReady = true;
			cache.sponsoredSkipped = true;
		}
		if (retry && cache.sponsoredSkipped) {
			cache.sponsoredReady = false;
			cache.sponsoredRequested = false;
			cache.sponsoredSkipped = false;
		}
	}
	for (const auto &[requestId, query] : _sponsoredRequests) {
		auto &cache = _cache[query];
		cache.sponsoredRequested = true;
		cache.sponsoredReady = !settings.show;
		cache.sponsoredSkipped = !settings.show;
	}
	if (!_query.isEmpty()) {
		const auto i = _cache.find(_query);
		_changes.fire((i != end(_cache) && i->second.peersReady)
			? i->second.result
			: PeerSearchResult{ .query = _query });
	}
}

void PeerSearch::request(
		const QString &query,
		Fn<void(PeerSearchResult)> callback,
		RequestType type) {
	using namespace Dialogs;
	_query = Api::ConvertPeerSearchQuery(query.trimmed());
	_callback = callback;
	if (_query.isEmpty()
		|| IsHashOrCashtagSearchQuery(_query) != HashOrCashtag::None) {
		finish(PeerSearchResult{});
		return;
	}
	auto &cache = _cache[_query];
	if (cache.peersReady && cache.sponsoredReady) {
		finish(cache.result);
		return;
	} else if (type == RequestType::CacheOnly) {
		_callback = nullptr;
		return;
	}
	cache.result.query = _query;
	if (_query.size() < kMinSponsoredQueryLength
		|| _type == Type::JustPeers) {
		cache.sponsoredReady = true;
	} else if (!cache.sponsoredReady && !cache.sponsoredRequested) {
		requestSponsored();
	}
	if (!cache.requested) {
		cache.requested = true;
		requestPeers();
	}
	if (cache.peersReady && cache.sponsoredReady) {
		finish(cache.result);
	}
}

void PeerSearch::requestPeers() {
	const auto requestId = _session->api().request(MTPcontacts_Search(
		MTP_flags(0),
		MTP_string(_query),
		MTP_int(SearchPeopleLimit)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		auto parsed = PeerSearchResult();
		parsed.my.reserve(data.vmy_results().v.size());
		for (const auto &id : data.vmy_results().v) {
			const auto peerId = peerFromMTP(id);
			parsed.my.push_back(_session->data().peer(peerId));
		}
		parsed.peers.reserve(data.vresults().v.size());
		for (const auto &id : data.vresults().v) {
			const auto peerId = peerFromMTP(id);
			parsed.peers.push_back(_session->data().peer(peerId));
		}
		finishPeers(requestId, std::move(parsed));
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		finishPeers(requestId, PeerSearchResult{});
	}).send();
	_peerRequests.emplace(requestId, _query);
}

void PeerSearch::requestSponsored() {
	auto &cache = _cache[_query];
	const auto settings = Core::App().settings().adSettings(
		Core::AdPlacement::Search);
	if (!settings.get) {
		cache.sponsoredReady = true;
		cache.sponsoredSkipped = true;
		return;
	}
	cache.sponsoredRequested = true;
	cache.sponsoredSkipped = !settings.show;
	cache.sponsoredReady = !settings.show;
	const auto requestId = _session->api().request(
		MTPcontacts_GetSponsoredPeers(MTP_string(_query))
	).done([=](
			const MTPcontacts_SponsoredPeers &result,
			mtpRequestId requestId) {
		if (!Core::App().settings().adSettings(Core::AdPlacement::Search).show) {
			finishSponsored(requestId, PeerSearchResult{});
			return;
		}
		result.match([&](const MTPDcontacts_sponsoredPeersEmpty &) {
			finishSponsored(requestId, PeerSearchResult{});
		}, [&](const MTPDcontacts_sponsoredPeers &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			auto parsed = PeerSearchResult();
			parsed.sponsored.reserve(data.vpeers().v.size());
			for (const auto &peer : data.vpeers().v) {
				const auto &data = peer.data();
				const auto peerId = peerFromMTP(data.vpeer());
				parsed.sponsored.push_back({
					.peer = _session->data().peer(peerId),
					.randomId = data.vrandom_id().v,
					.sponsorInfo = TextWithEntities::Simple(
						qs(data.vsponsor_info().value_or_empty())),
					.additionalInfo = TextWithEntities::Simple(
						qs(data.vadditional_info().value_or_empty())),
				});
			}
			finishSponsored(requestId, std::move(parsed));
		});
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		finishSponsored(requestId, PeerSearchResult{});
	}).send();
	_sponsoredRequests.emplace(requestId, _query);
}

void PeerSearch::finishPeers(
		mtpRequestId requestId,
		PeerSearchResult result) {
	const auto query = _peerRequests.take(requestId);
	Assert(query.has_value());

	auto &cache = _cache[*query];
	cache.peersReady = true;
	cache.result.my = std::move(result.my);
	cache.result.peers = std::move(result.peers);
	if (cache.sponsoredReady && _query == *query) {
		finish(cache.result);
	}
}

void PeerSearch::finishSponsored(
		mtpRequestId requestId,
		PeerSearchResult result) {
	const auto query = _sponsoredRequests.take(requestId);
	Assert(query.has_value());

	auto &cache = _cache[*query];
	cache.sponsoredReady = true;
	cache.sponsoredSkipped = !Core::App().settings().adSettings(
		Core::AdPlacement::Search).show;
	cache.result.sponsored = std::move(result.sponsored);
	if (cache.peersReady && _query == *query) {
		if (_callback) {
			finish(cache.result);
		} else if (_lastResultQuery == *query
			&& !cache.result.sponsored.empty()) {
			_changes.fire_copy(cache.result);
		}
	}
}

void PeerSearch::finish(PeerSearchResult result) {
	if (const auto onstack = base::take(_callback)) {
		_lastResultQuery = result.query;
		onstack(std::move(result));
	}
}

void PeerSearch::clear() {
	_query = QString();
	_lastResultQuery = QString();
	_callback = nullptr;
	_cache.clear();
	for (const auto &[requestId, query] : base::take(_peerRequests)) {
		_session->api().request(requestId).cancel();
	}
	for (const auto &[requestId, query] : base::take(_sponsoredRequests)) {
		_session->api().request(requestId).cancel();
	}
}

} // namespace Api
