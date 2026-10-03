/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/ad_settings.h"

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct SponsoredSearchResult {
    not_null<PeerData*> peer;
    QByteArray randomId;
    TextWithEntities sponsorInfo;
    TextWithEntities additionalInfo;
};

struct PeerSearchResult {
    QString query;
    std::vector<not_null<PeerData*>> my;
    std::vector<not_null<PeerData*>> peers;
    std::vector<SponsoredSearchResult> sponsored;
};

class PeerSearch final {
public:
    enum class Type {
        WithSponsored,
        JustPeers,
    };
    PeerSearch(not_null<Main::Session*> session, Type type);
    ~PeerSearch();

    enum class RequestType {
        CacheOnly,
        CacheOrRemote,
    };
    void request(
        const QString &query,
        Fn<void(PeerSearchResult)> callback,
        RequestType type = RequestType::CacheOrRemote);
    void clear();
	[[nodiscard]] rpl::producer<PeerSearchResult> changes() const;

private:
    struct CacheEntry {
        PeerSearchResult result;
        bool requested = false;
        bool peersReady = false;
        bool sponsoredReady = false;
		bool sponsoredRequested = false;
		bool sponsoredSkipped = false;
    };

    void requestPeers();
    void requestSponsored();
	void applyAdSettings(Core::AdSettings settings);

	void finish(PeerSearchResult result);
	void finishPeers(mtpRequestId requestId, PeerSearchResult result);
    void finishSponsored(mtpRequestId requestId, PeerSearchResult result);

    const not_null<Main::Session*> _session;
    const Type _type;
    Core::AdSettings _adSettings;

    QString _query;
	QString _lastResultQuery;
    Fn<void(PeerSearchResult)> _callback;

	base::flat_map<QString, CacheEntry> _cache;
	base::flat_map<mtpRequestId, QString> _peerRequests;
	base::flat_map<mtpRequestId, QString> _sponsoredRequests;
	rpl::event_stream<PeerSearchResult> _changes;
	rpl::lifetime _lifetime;

};

} // namespace Api
