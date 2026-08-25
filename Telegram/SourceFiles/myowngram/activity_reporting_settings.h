/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MyOwnGram::ActivityReporting {

enum class StoryAction {
	View,
	Reaction,
	Reply,
	Share,
	Link,
	LiveJoin,
	LiveReaction,
	LiveComment,
};

enum class StoryActionPolicy {
	Allow,
	Ask,
	Block,
};

[[nodiscard]] bool SendTypingStatus();
void SetSendTypingStatus(bool enabled);

[[nodiscard]] StoryActionPolicy StoryPolicy(StoryAction action);
[[nodiscard]] rpl::producer<StoryActionPolicy> StoryPolicyChanges(
	StoryAction action);
void SetStoryPolicy(StoryAction action, StoryActionPolicy policy);

[[nodiscard]] bool RememberWatchedStories();
[[nodiscard]] rpl::producer<bool> RememberWatchedStoriesChanges();
void SetRememberWatchedStories(bool enabled);

[[nodiscard]] bool SendReadMetrics();
[[nodiscard]] rpl::producer<bool> SendReadMetricsChanges();
void SetSendReadMetrics(bool enabled);

[[nodiscard]] bool SendMusicListenReports();
[[nodiscard]] rpl::producer<bool> SendMusicListenReportsChanges();
void SetSendMusicListenReports(bool enabled);

[[nodiscard]] bool SendPremiumPromoAnalytics();
void SetSendPremiumPromoAnalytics(bool enabled);

[[nodiscard]] bool UploadCallDiagnostics();
void SetUploadCallDiagnostics(bool enabled);

[[nodiscard]] bool SendGatewayDeliveryReports();
[[nodiscard]] rpl::producer<bool> SendGatewayDeliveryReportsChanges();
void SetSendGatewayDeliveryReports(bool enabled);

} // namespace MyOwnGram::ActivityReporting
