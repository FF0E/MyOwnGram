/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "myowngram/sponsored_content_settings.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace MyOwnGram::SponsoredContent {
namespace {

constexpr auto kChannelReceiveOnly =
	"myowngram.sponsored.delivery.channel.receive_only";
constexpr auto kChannelBlockRequests =
	"myowngram.sponsored.delivery.channel.block_requests";
constexpr auto kBotReceiveOnly =
	"myowngram.sponsored.delivery.bot.receive_only";
constexpr auto kBotBlockRequests =
	"myowngram.sponsored.delivery.bot.block_requests";
constexpr auto kVideoReceiveOnly =
	"myowngram.sponsored.delivery.video.receive_only";
constexpr auto kVideoBlockRequests =
	"myowngram.sponsored.delivery.video.block_requests";
constexpr auto kSearchReceiveOnly =
	"myowngram.sponsored.delivery.search.receive_only";
constexpr auto kSearchBlockRequests =
	"myowngram.sponsored.delivery.search.block_requests";
constexpr auto kRandomViewReports = "myowngram.sponsored.view.random";
constexpr auto kNeverViewReports = "myowngram.sponsored.view.never";
constexpr auto kOpenDestinations = "myowngram.sponsored.open_destinations";
constexpr auto kSendClickReports = "myowngram.sponsored.send_click_reports";

rpl::event_stream<Surface> gDeliveryChanges;
rpl::event_stream<ViewReportingMode> gViewReportingChanges;

struct DeliveryKeys {
	std::string_view receiveOnly;
	std::string_view blockRequests;
};

[[nodiscard]] Core::Settings &Settings() {
	return Core::App().settings();
}

[[nodiscard]] DeliveryKeys KeysFor(Surface surface) {
	switch (surface) {
	case Surface::Channel:
		return { kChannelReceiveOnly, kChannelBlockRequests };
	case Surface::Bot:
		return { kBotReceiveOnly, kBotBlockRequests };
	case Surface::Video:
		return { kVideoReceiveOnly, kVideoBlockRequests };
	case Surface::Search:
		return { kSearchReceiveOnly, kSearchBlockRequests };
	}
	Unexpected("Surface in SponsoredContent::KeysFor.");
}

[[nodiscard]] constexpr DeliveryMode ResolveDelivery(
		bool receiveOnly,
		bool blockRequests) {
	return blockRequests
		? DeliveryMode::BlockRequests
		: receiveOnly
		? DeliveryMode::ReceiveOnly
		: DeliveryMode::Show;
}

[[nodiscard]] constexpr ViewReportingMode ResolveViewReporting(
		bool random,
		bool never) {
	return never
		? ViewReportingMode::Never
		: random
		? ViewReportingMode::RandomAfterReceipt
		: ViewReportingMode::WhenVisible;
}

static_assert(
	ResolveDelivery(false, false) == DeliveryMode::Show
	&& ResolveDelivery(true, false) == DeliveryMode::ReceiveOnly
	&& ResolveDelivery(false, true) == DeliveryMode::BlockRequests
	&& ResolveDelivery(true, true) == DeliveryMode::BlockRequests);
static_assert(
	ResolveViewReporting(false, false) == ViewReportingMode::WhenVisible
	&& ResolveViewReporting(true, false)
		== ViewReportingMode::RandomAfterReceipt
	&& ResolveViewReporting(false, true) == ViewReportingMode::Never
	&& ResolveViewReporting(true, true) == ViewReportingMode::Never);

} // namespace

DeliveryMode Delivery(Surface surface) {
	const auto keys = KeysFor(surface);
	return ResolveDelivery(
		Settings().readPref<bool>(keys.receiveOnly, false),
		Settings().readPref<bool>(keys.blockRequests, false));
}

void SetDelivery(Surface surface, DeliveryMode mode) {
	if (Delivery(surface) == mode) {
		return;
	}
	const auto keys = KeysFor(surface);
	Settings().writePref<bool>(
		keys.receiveOnly,
		mode == DeliveryMode::ReceiveOnly);
	Settings().writePref<bool>(
		keys.blockRequests,
		mode == DeliveryMode::BlockRequests);
	gDeliveryChanges.fire_copy(surface);
}

rpl::producer<DeliveryMode> DeliveryValue(Surface surface) {
	return rpl::single(Delivery(surface)) | rpl::then(
		gDeliveryChanges.events(
		) | rpl::filter([=](Surface changed) {
			return changed == surface;
		}) | rpl::map([=](Surface) {
			return Delivery(surface);
		})
	) | rpl::distinct_until_changed();
}

bool ShouldRequest(Surface surface) {
	return Delivery(surface) != DeliveryMode::BlockRequests;
}

bool ShouldDisplay(Surface surface) {
	return Delivery(surface) == DeliveryMode::Show;
}

rpl::producer<Surface> DeliveryChanges() {
	return gDeliveryChanges.events();
}

ViewReportingMode ViewReporting() {
	return ResolveViewReporting(
		Settings().readPref<bool>(kRandomViewReports, false),
		Settings().readPref<bool>(kNeverViewReports, false));
}

void SetViewReporting(ViewReportingMode mode) {
	if (ViewReporting() == mode) {
		return;
	}
	Settings().writePref<bool>(
		kRandomViewReports,
		mode == ViewReportingMode::RandomAfterReceipt);
	Settings().writePref<bool>(
		kNeverViewReports,
		mode == ViewReportingMode::Never);
	gViewReportingChanges.fire_copy(mode);
}

rpl::producer<ViewReportingMode> ViewReportingValue() {
	return rpl::single(ViewReporting()) | rpl::then(
		gViewReportingChanges.events()
	) | rpl::distinct_until_changed();
}

rpl::producer<ViewReportingMode> ViewReportingChanges() {
	return gViewReportingChanges.events();
}

bool OpenDestinations() {
	return Settings().readPref<bool>(kOpenDestinations, true);
}

void SetOpenDestinations(bool enabled) {
	if (OpenDestinations() == enabled) {
		return;
	}
	Settings().writePref<bool>(kOpenDestinations, enabled);
}

bool SendClickReports() {
	return Settings().readPref<bool>(kSendClickReports, true);
}

void SetSendClickReports(bool enabled) {
	if (SendClickReports() == enabled) {
		return;
	}
	Settings().writePref<bool>(kSendClickReports, enabled);
}

} // namespace MyOwnGram::SponsoredContent
