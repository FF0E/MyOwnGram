/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "myowngram/activity_reporting_settings.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace MyOwnGram::ActivityReporting {
namespace {

struct Setting {
	std::string_view key;
	bool fallback = true;
	rpl::event_stream<bool> changes;
};

struct StoryPolicySetting {
	Setting allow;
	Setting ask;
	rpl::event_stream<StoryActionPolicy> changes;
};

Setting SendTypingStatusState = {
	.key = "myowngram.activity_reporting.send_typing_status",
};
StoryPolicySetting StoryViewState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_story_view_reports",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_story_view_reports",
		.fallback = false,
	},
};
StoryPolicySetting StoryReactionState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_story_reactions",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_story_reactions",
		.fallback = false,
	},
};
StoryPolicySetting StoryReplyState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_story_replies",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_story_replies",
		.fallback = false,
	},
};
StoryPolicySetting StoryShareState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_story_shares",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_story_shares",
		.fallback = false,
	},
};
StoryPolicySetting StoryLinkState = {
	.allow = {
		.key = "myowngram.activity_reporting.open_story_links",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_story_links",
		.fallback = false,
	},
};
StoryPolicySetting LiveStoryJoinState = {
	.allow = {
		.key = "myowngram.activity_reporting.join_live_stories",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_live_story_joins",
		.fallback = false,
	},
};
StoryPolicySetting LiveStoryReactionState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_live_story_reactions",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_live_story_reactions",
		.fallback = false,
	},
};
StoryPolicySetting LiveStoryCommentState = {
	.allow = {
		.key = "myowngram.activity_reporting.send_live_story_comments",
	},
	.ask = {
		.key = "myowngram.activity_reporting.ask_live_story_comments",
		.fallback = false,
	},
};
Setting RememberWatchedStoriesState = {
	.key = "myowngram.activity_reporting.remember_watched_stories",
	.fallback = false,
};
Setting SendReadMetricsState = {
	.key = "myowngram.activity_reporting.send_read_metrics",
};
Setting SendMusicListenReportsState = {
	.key = "myowngram.activity_reporting.send_music_listen_reports",
};
Setting SendPremiumPromoAnalyticsState = {
	.key = "myowngram.activity_reporting.send_premium_promo_analytics",
};
Setting UploadCallDiagnosticsState = {
	.key = "myowngram.activity_reporting.upload_call_diagnostics",
};
Setting SendGatewayDeliveryReportsState = {
	.key = "myowngram.activity_reporting.send_gateway_delivery_reports",
};

bool Read(const Setting &setting) {
	return Core::App().settings().readPref<bool>(
		setting.key,
		setting.fallback);
}

rpl::producer<bool> Changes(Setting &setting) {
	return setting.changes.events();
}

void Write(Setting &setting, bool enabled) {
	if (Read(setting) == enabled) {
		return;
	}
	Core::App().settings().writePref<bool>(setting.key, enabled);
	setting.changes.fire_copy(enabled);
}

StoryPolicySetting &PolicySetting(StoryAction action) {
	switch (action) {
	case StoryAction::View:
		return StoryViewState;
	case StoryAction::Reaction:
		return StoryReactionState;
	case StoryAction::Reply:
		return StoryReplyState;
	case StoryAction::Share:
		return StoryShareState;
	case StoryAction::Link:
		return StoryLinkState;
	case StoryAction::LiveJoin:
		return LiveStoryJoinState;
	case StoryAction::LiveReaction:
		return LiveStoryReactionState;
	case StoryAction::LiveComment:
		return LiveStoryCommentState;
	}
	Unexpected("StoryAction value.");
}

} // namespace

bool SendTypingStatus() {
	return Read(SendTypingStatusState);
}

void SetSendTypingStatus(bool enabled) {
	Write(SendTypingStatusState, enabled);
}

StoryActionPolicy StoryPolicy(StoryAction action) {
	const auto &setting = PolicySetting(action);
	return !Read(setting.allow)
		? StoryActionPolicy::Block
		: Read(setting.ask)
		? StoryActionPolicy::Ask
		: StoryActionPolicy::Allow;
}

rpl::producer<StoryActionPolicy> StoryPolicyChanges(StoryAction action) {
	return PolicySetting(action).changes.events();
}

void SetStoryPolicy(StoryAction action, StoryActionPolicy policy) {
	auto &setting = PolicySetting(action);
	if (StoryPolicy(action) == policy) {
		return;
	}
	switch (policy) {
	case StoryActionPolicy::Allow:
		Write(setting.ask, false);
		Write(setting.allow, true);
		break;
	case StoryActionPolicy::Ask:
		Write(setting.ask, true);
		Write(setting.allow, true);
		break;
	case StoryActionPolicy::Block:
		Write(setting.allow, false);
		Write(setting.ask, false);
		break;
	}
	setting.changes.fire_copy(policy);
}

bool RememberWatchedStories() {
	return Read(RememberWatchedStoriesState);
}

rpl::producer<bool> RememberWatchedStoriesChanges() {
	return Changes(RememberWatchedStoriesState);
}

void SetRememberWatchedStories(bool enabled) {
	Write(RememberWatchedStoriesState, enabled);
}

bool SendReadMetrics() {
	return Read(SendReadMetricsState);
}

rpl::producer<bool> SendReadMetricsChanges() {
	return Changes(SendReadMetricsState);
}

void SetSendReadMetrics(bool enabled) {
	Write(SendReadMetricsState, enabled);
}

bool SendMusicListenReports() {
	return Read(SendMusicListenReportsState);
}

rpl::producer<bool> SendMusicListenReportsChanges() {
	return Changes(SendMusicListenReportsState);
}

void SetSendMusicListenReports(bool enabled) {
	Write(SendMusicListenReportsState, enabled);
}

bool SendPremiumPromoAnalytics() {
	return Read(SendPremiumPromoAnalyticsState);
}

void SetSendPremiumPromoAnalytics(bool enabled) {
	Write(SendPremiumPromoAnalyticsState, enabled);
}

bool UploadCallDiagnostics() {
	return Read(UploadCallDiagnosticsState);
}

void SetUploadCallDiagnostics(bool enabled) {
	Write(UploadCallDiagnosticsState, enabled);
}

bool SendGatewayDeliveryReports() {
	return Read(SendGatewayDeliveryReportsState);
}

rpl::producer<bool> SendGatewayDeliveryReportsChanges() {
	return Changes(SendGatewayDeliveryReportsState);
}

void SetSendGatewayDeliveryReports(bool enabled) {
	Write(SendGatewayDeliveryReportsState, enabled);
}

} // namespace MyOwnGram::ActivityReporting
