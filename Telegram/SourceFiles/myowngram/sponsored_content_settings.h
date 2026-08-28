/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MyOwnGram::SponsoredContent {

enum class DeliveryMode {
	Show,
	ReceiveOnly,
	BlockRequests,
};

enum class Surface {
	Channel,
	Bot,
	Video,
	Search,
};

enum class ViewReportingMode {
	WhenVisible,
	RandomAfterReceipt,
	Never,
};

[[nodiscard]] DeliveryMode Delivery(Surface surface);
void SetDelivery(Surface surface, DeliveryMode mode);
[[nodiscard]] rpl::producer<DeliveryMode> DeliveryValue(Surface surface);

[[nodiscard]] bool ShouldRequest(Surface surface);
[[nodiscard]] bool ShouldDisplay(Surface surface);
[[nodiscard]] rpl::producer<Surface> DeliveryChanges();

[[nodiscard]] ViewReportingMode ViewReporting();
void SetViewReporting(ViewReportingMode mode);
[[nodiscard]] rpl::producer<ViewReportingMode> ViewReportingValue();
[[nodiscard]] rpl::producer<ViewReportingMode> ViewReportingChanges();

[[nodiscard]] bool OpenDestinations();
void SetOpenDestinations(bool enabled);

[[nodiscard]] bool SendClickReports();
void SetSendClickReports(bool enabled);

} // namespace MyOwnGram::SponsoredContent
