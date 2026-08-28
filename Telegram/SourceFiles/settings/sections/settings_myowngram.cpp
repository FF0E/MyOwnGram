/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_myowngram.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "myowngram/activity_reporting_settings.h"
#include "myowngram/mini_app_settings.h"
#include "myowngram/sponsored_content_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

namespace ActivityReporting = ::MyOwnGram::ActivityReporting;
namespace MiniApps = ::MyOwnGram::MiniApps;
namespace SponsoredContent = ::MyOwnGram::SponsoredContent;

class MyOwnGram final : public Section<MyOwnGram> {
public:
	MyOwnGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class MyOwnGramGeneral final : public Section<MyOwnGramGeneral> {
public:
	MyOwnGramGeneral(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class MyOwnGramPrivacy final : public Section<MyOwnGramPrivacy> {
public:
	MyOwnGramPrivacy(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class MyOwnGramDataSharing final : public Section<MyOwnGramDataSharing> {
public:
	MyOwnGramDataSharing(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

class MyOwnGramSponsoredContent final
	: public Section<MyOwnGramSponsoredContent> {
public:
	MyOwnGramSponsoredContent(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

void AddMyOwnGramToggle(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		Fn<bool()> getter,
		Fn<void(bool)> setter,
		QStringList keywords) {
	const auto button = builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(getter()),
		.keywords = std::move(keywords),
	});
	if (button) {
		button->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return enabled != getter();
		}) | rpl::on_next([=](bool enabled) {
			setter(enabled);
		}, button->lifetime());
	}
}

void AddMyOwnGramGroupFooter(
		SectionBuilder &builder,
		rpl::producer<QString> text) {
	builder.addSkip();
	builder.addDividerText(std::move(text));
}

QString MiniAppOpenModeText(MiniApps::OpenMode mode) {
	switch (mode) {
	case MiniApps::OpenMode::Internal:
		return tr::lng_myowngram_open_mini_apps_internal(tr::now);
	case MiniApps::OpenMode::Ask:
		return tr::lng_myowngram_open_mini_apps_ask(tr::now);
	case MiniApps::OpenMode::Browser:
		return tr::lng_myowngram_open_mini_apps_browser(tr::now);
	}
	Unexpected("MiniApps::OpenMode value.");
}

rpl::producer<QString> MiniAppOpenModeTextValue(
		MiniApps::OpenMode mode) {
	switch (mode) {
	case MiniApps::OpenMode::Internal:
		return tr::lng_myowngram_open_mini_apps_internal();
	case MiniApps::OpenMode::Ask:
		return tr::lng_myowngram_open_mini_apps_ask();
	case MiniApps::OpenMode::Browser:
		return tr::lng_myowngram_open_mini_apps_browser();
	}
	Unexpected("MiniApps::OpenMode value.");
}

rpl::producer<MiniApps::OpenMode> MiniAppOpenModeValue() {
	return rpl::single(MiniApps::Mode())
		| rpl::then(MiniApps::ModeChanges());
}

void AddMiniAppOpenMode(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"myowngram/general/open_mini_apps"_q,
		.title = tr::lng_myowngram_open_mini_apps(),
		.st = &st::settingsButtonNoIcon,
		.label = MiniAppOpenModeValue(
		) | rpl::map(MiniAppOpenModeTextValue) | rpl::flatten_latest(),
		.onClick = [=] {
			const auto options = std::vector{
				MiniAppOpenModeText(MiniApps::OpenMode::Internal),
				MiniAppOpenModeText(MiniApps::OpenMode::Ask),
				MiniAppOpenModeText(MiniApps::OpenMode::Browser),
			};
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::lng_myowngram_open_mini_apps(),
					.options = options,
					.initialSelection = static_cast<int>(MiniApps::Mode()),
					.callback = [](int index) {
						MiniApps::SetMode(
							static_cast<MiniApps::OpenMode>(index));
					},
				});
			}));
		},
		.keywords = {
			u"mini apps"_q,
			u"browser"_q,
			u"web"_q,
			u"ask"_q,
		},
	});
}

QString SponsoredDeliveryText(SponsoredContent::DeliveryMode mode) {
	switch (mode) {
	case SponsoredContent::DeliveryMode::Show:
		return tr::lng_myowngram_sponsored_delivery_show(tr::now);
	case SponsoredContent::DeliveryMode::ReceiveOnly:
		return tr::lng_myowngram_sponsored_delivery_receive(tr::now);
	case SponsoredContent::DeliveryMode::BlockRequests:
		return tr::lng_myowngram_sponsored_delivery_block(tr::now);
	}
	Unexpected("SponsoredContent::DeliveryMode value.");
}

rpl::producer<QString> SponsoredDeliveryTextValue(
		SponsoredContent::DeliveryMode mode) {
	switch (mode) {
	case SponsoredContent::DeliveryMode::Show:
		return tr::lng_myowngram_sponsored_delivery_show();
	case SponsoredContent::DeliveryMode::ReceiveOnly:
		return tr::lng_myowngram_sponsored_delivery_receive();
	case SponsoredContent::DeliveryMode::BlockRequests:
		return tr::lng_myowngram_sponsored_delivery_block();
	}
	Unexpected("SponsoredContent::DeliveryMode value.");
}

void AddSponsoredDeliveryMode(
		SectionBuilder &builder,
		SponsoredContent::Surface surface,
		QString id,
		rpl::producer<QString> title) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = std::move(id),
		.title = std::move(title),
		.st = &st::settingsButtonNoIcon,
		.label = SponsoredContent::DeliveryValue(surface)
			| rpl::map(SponsoredDeliveryTextValue)
			| rpl::flatten_latest(),
		.onClick = [=] {
			const auto options = std::vector{
				SponsoredDeliveryText(SponsoredContent::DeliveryMode::Show),
				SponsoredDeliveryText(
					SponsoredContent::DeliveryMode::ReceiveOnly),
				SponsoredDeliveryText(
					SponsoredContent::DeliveryMode::BlockRequests),
			};
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::lng_myowngram_sponsored_delivery_mode(),
					.options = options,
					.initialSelection = static_cast<int>(
						SponsoredContent::Delivery(surface)),
					.callback = [=](int index) {
						const auto mode = static_cast<
							SponsoredContent::DeliveryMode>(index);
						SponsoredContent::SetDelivery(surface, mode);
					},
				});
			}));
		},
		.keywords = {
			u"ads"_q,
			u"sponsored"_q,
			u"receive"_q,
			u"block"_q,
		},
	});
}

QString SponsoredViewReportingText(
		SponsoredContent::ViewReportingMode mode) {
	switch (mode) {
	case SponsoredContent::ViewReportingMode::WhenVisible:
		return tr::lng_myowngram_sponsored_view_visible(tr::now);
	case SponsoredContent::ViewReportingMode::RandomAfterReceipt:
		return tr::lng_myowngram_sponsored_view_random(tr::now);
	case SponsoredContent::ViewReportingMode::Never:
		return tr::lng_myowngram_sponsored_view_never(tr::now);
	}
	Unexpected("SponsoredContent::ViewReportingMode value.");
}

rpl::producer<QString> SponsoredViewReportingTextValue(
		SponsoredContent::ViewReportingMode mode) {
	switch (mode) {
	case SponsoredContent::ViewReportingMode::WhenVisible:
		return tr::lng_myowngram_sponsored_view_visible();
	case SponsoredContent::ViewReportingMode::RandomAfterReceipt:
		return tr::lng_myowngram_sponsored_view_random();
	case SponsoredContent::ViewReportingMode::Never:
		return tr::lng_myowngram_sponsored_view_never();
	}
	Unexpected("SponsoredContent::ViewReportingMode value.");
}

void AddSponsoredViewReporting(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"myowngram/sponsored/view_reporting"_q,
		.title = tr::lng_myowngram_sponsored_view_reporting(),
		.st = &st::settingsButtonNoIcon,
		.label = SponsoredContent::ViewReportingValue(
		) | rpl::map(SponsoredViewReportingTextValue) | rpl::flatten_latest(),
		.onClick = [=] {
			const auto options = std::vector{
				SponsoredViewReportingText(
					SponsoredContent::ViewReportingMode::WhenVisible),
				SponsoredViewReportingText(
					SponsoredContent::ViewReportingMode::RandomAfterReceipt),
				SponsoredViewReportingText(
					SponsoredContent::ViewReportingMode::Never),
			};
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::lng_myowngram_sponsored_view_reporting(),
					.options = options,
					.initialSelection = static_cast<int>(
						SponsoredContent::ViewReporting()),
					.callback = [](int index) {
						const auto mode = static_cast<
							SponsoredContent::ViewReportingMode>(index);
						SponsoredContent::SetViewReporting(mode);
					},
				});
			}));
		},
		.keywords = {
			u"ads"_q,
			u"sponsored"_q,
			u"view"_q,
			u"report"_q,
		},
	});
}

QString StoryPolicyText(ActivityReporting::StoryActionPolicy policy) {
	switch (policy) {
	case ActivityReporting::StoryActionPolicy::Allow:
		return tr::lng_myowngram_story_policy_allow(tr::now);
	case ActivityReporting::StoryActionPolicy::Ask:
		return tr::lng_myowngram_story_policy_ask(tr::now);
	case ActivityReporting::StoryActionPolicy::Block:
		return tr::lng_myowngram_story_policy_block(tr::now);
	}
	Unexpected("StoryActionPolicy value.");
}

rpl::producer<QString> StoryPolicyTextValue(
		ActivityReporting::StoryActionPolicy policy) {
	switch (policy) {
	case ActivityReporting::StoryActionPolicy::Allow:
		return tr::lng_myowngram_story_policy_allow();
	case ActivityReporting::StoryActionPolicy::Ask:
		return tr::lng_myowngram_story_policy_ask();
	case ActivityReporting::StoryActionPolicy::Block:
		return tr::lng_myowngram_story_policy_block();
	}
	Unexpected("StoryActionPolicy value.");
}

rpl::producer<ActivityReporting::StoryActionPolicy> StoryPolicyValue(
		ActivityReporting::StoryAction action) {
	return rpl::single(ActivityReporting::StoryPolicy(action))
		| rpl::then(ActivityReporting::StoryPolicyChanges(action));
}

rpl::producer<QString> StoryActionTitle(
		ActivityReporting::StoryAction action) {
	switch (action) {
	case ActivityReporting::StoryAction::View:
		return tr::lng_myowngram_story_view_reports();
	case ActivityReporting::StoryAction::Reaction:
		return tr::lng_myowngram_story_reactions();
	case ActivityReporting::StoryAction::Reply:
		return tr::lng_myowngram_story_replies();
	case ActivityReporting::StoryAction::Share:
		return tr::lng_myowngram_story_shares();
	case ActivityReporting::StoryAction::Link:
		return tr::lng_myowngram_story_links();
	case ActivityReporting::StoryAction::LiveJoin:
		return tr::lng_myowngram_live_story_joins();
	case ActivityReporting::StoryAction::LiveReaction:
		return tr::lng_myowngram_live_story_reactions();
	case ActivityReporting::StoryAction::LiveComment:
		return tr::lng_myowngram_live_story_comments();
	}
	Unexpected("StoryAction value.");
}

QString StoryActionId(ActivityReporting::StoryAction action) {
	switch (action) {
	case ActivityReporting::StoryAction::View:
		return u"myowngram/privacy/story_view_reports"_q;
	case ActivityReporting::StoryAction::Reaction:
		return u"myowngram/privacy/story_reactions"_q;
	case ActivityReporting::StoryAction::Reply:
		return u"myowngram/privacy/story_replies"_q;
	case ActivityReporting::StoryAction::Share:
		return u"myowngram/privacy/story_shares"_q;
	case ActivityReporting::StoryAction::Link:
		return u"myowngram/privacy/story_links"_q;
	case ActivityReporting::StoryAction::LiveJoin:
		return u"myowngram/privacy/live_story_joins"_q;
	case ActivityReporting::StoryAction::LiveReaction:
		return u"myowngram/privacy/live_story_reactions"_q;
	case ActivityReporting::StoryAction::LiveComment:
		return u"myowngram/privacy/live_story_comments"_q;
	}
	Unexpected("StoryAction value.");
}

QStringList StoryActionKeywords(ActivityReporting::StoryAction action) {
	auto result = QStringList{
		u"stories"_q,
		u"ask"_q,
		u"activity"_q,
		u"privacy"_q,
	};
	switch (action) {
	case ActivityReporting::StoryAction::View:
		result.push_back(u"views"_q);
		break;
	case ActivityReporting::StoryAction::Reaction:
		result.push_back(u"reactions"_q);
		break;
	case ActivityReporting::StoryAction::Reply:
		result.push_back(u"replies"_q);
		result.push_back(u"comments"_q);
		break;
	case ActivityReporting::StoryAction::Share:
		result.push_back(u"shares"_q);
		result.push_back(u"forwards"_q);
		break;
	case ActivityReporting::StoryAction::Link:
		result.push_back(u"links"_q);
		break;
	case ActivityReporting::StoryAction::LiveJoin:
		result.push_back(u"live"_q);
		result.push_back(u"calls"_q);
		result.push_back(u"joins"_q);
		break;
	case ActivityReporting::StoryAction::LiveReaction:
		result.push_back(u"live"_q);
		result.push_back(u"reactions"_q);
		break;
	case ActivityReporting::StoryAction::LiveComment:
		result.push_back(u"live"_q);
		result.push_back(u"comments"_q);
		break;
	}
	return result;
}

void AddStoryPolicy(
		SectionBuilder &builder,
		ActivityReporting::StoryAction action) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}
	builder.addButton({
		.id = StoryActionId(action),
		.title = StoryActionTitle(action),
		.st = &st::settingsButtonNoIcon,
		.label = StoryPolicyValue(action
		) | rpl::map(StoryPolicyTextValue) | rpl::flatten_latest(),
		.onClick = [=] {
			const auto options = std::vector{
				StoryPolicyText(ActivityReporting::StoryActionPolicy::Allow),
				StoryPolicyText(ActivityReporting::StoryActionPolicy::Ask),
				StoryPolicyText(ActivityReporting::StoryActionPolicy::Block),
			};
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = StoryActionTitle(action),
					.options = options,
					.initialSelection = static_cast<int>(
						ActivityReporting::StoryPolicy(action)),
					.callback = [=](int index) {
						ActivityReporting::SetStoryPolicy(
							action,
							static_cast<ActivityReporting::StoryActionPolicy>(
								index));
					},
				});
			}));
		},
		.keywords = StoryActionKeywords(action),
	});
}

void BuildGeneralSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/general/web_content"_q,
		.title = tr::lng_myowngram_web_content(),
		.keywords = { u"web"_q, u"browser"_q, u"mini apps"_q },
	});
	AddMiniAppOpenMode(builder);
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_open_mini_apps_about());

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/general/app_data"_q,
		.title = tr::lng_myowngram_app_data(),
		.keywords = { u"app"_q, u"data"_q, u"preferences"_q },
	});
	AddMyOwnGramToggle(
		builder,
		u"myowngram/general/keep_preferences"_q,
		tr::lng_myowngram_keep_preferences_on_last_logout(),
		[=] { return settings->keepPreferencesOnLastLogout(); },
		[=](bool enabled) {
			settings->setKeepPreferencesOnLastLogout(enabled);
		},
		{ u"logout"_q, u"accounts"_q, u"preferences"_q, u"settings"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_keep_preferences_on_last_logout_about());
}

void BuildPrivacySection(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/privacy/activity_visibility"_q,
		.title = tr::lng_myowngram_activity_visibility(),
		.keywords = { u"activity"_q, u"visibility"_q, u"privacy"_q },
	});
	AddMyOwnGramToggle(
		builder,
		u"myowngram/privacy/share_typing_status"_q,
		tr::lng_myowngram_send_typing_status(),
		ActivityReporting::SendTypingStatus,
		ActivityReporting::SetSendTypingStatus,
		{ u"typing"_q, u"status"_q, u"activity"_q, u"privacy"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_send_typing_status_about());

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/privacy/story_activity"_q,
		.title = tr::lng_myowngram_story_activity(),
		.keywords = { u"stories"_q, u"views"_q, u"activity"_q },
	});
	AddStoryPolicy(builder, ActivityReporting::StoryAction::View);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::Reaction);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::Reply);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::Share);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::Link);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::LiveJoin);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::LiveReaction);
	AddStoryPolicy(builder, ActivityReporting::StoryAction::LiveComment);
	const auto remember = builder.addButton({
		.id = u"myowngram/privacy/remember_watched_stories"_q,
		.title = tr::lng_myowngram_remember_watched_stories(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(
			ActivityReporting::RememberWatchedStories()
		) | rpl::then(ActivityReporting::RememberWatchedStoriesChanges()),
		.keywords = {
			u"stories"_q,
			u"watched"_q,
			u"remember"_q,
			u"storage"_q,
		},
		.shown = StoryPolicyValue(
			ActivityReporting::StoryAction::View
		) | rpl::map([](ActivityReporting::StoryActionPolicy policy) {
			return policy != ActivityReporting::StoryActionPolicy::Allow;
		}),
	});
	if (remember) {
		remember->toggledValue(
		) | rpl::filter([](bool enabled) {
			return enabled
				!= ActivityReporting::RememberWatchedStories();
		}) | rpl::on_next([](bool enabled) {
			ActivityReporting::SetRememberWatchedStories(enabled);
		}, remember->lifetime());
	}
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_story_activity_about());
}

void BuildSponsoredContentSection(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/sponsored/delivery"_q,
		.title = tr::lng_myowngram_sponsored_delivery(),
		.keywords = { u"ads"_q, u"sponsored"_q, u"delivery"_q },
	});
	AddSponsoredDeliveryMode(
		builder,
		SponsoredContent::Surface::Channel,
		u"myowngram/sponsored/channel"_q,
		tr::lng_myowngram_sponsored_channel());
	AddSponsoredDeliveryMode(
		builder,
		SponsoredContent::Surface::Bot,
		u"myowngram/sponsored/bot"_q,
		tr::lng_myowngram_sponsored_bot());
	AddSponsoredDeliveryMode(
		builder,
		SponsoredContent::Surface::Video,
		u"myowngram/sponsored/video"_q,
		tr::lng_myowngram_sponsored_video());
	AddSponsoredDeliveryMode(
		builder,
		SponsoredContent::Surface::Search,
		u"myowngram/sponsored/search"_q,
		tr::lng_myowngram_sponsored_search());
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_sponsored_delivery_about());

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/sponsored/reporting_links"_q,
		.title = tr::lng_myowngram_sponsored_reporting_links(),
		.keywords = { u"ads"_q, u"reporting"_q, u"links"_q },
	});
	AddSponsoredViewReporting(builder);
	AddMyOwnGramToggle(
		builder,
		u"myowngram/sponsored/open_destinations"_q,
		tr::lng_myowngram_sponsored_open_destinations(),
		SponsoredContent::OpenDestinations,
		SponsoredContent::SetOpenDestinations,
		{ u"ads"_q, u"links"_q, u"buttons"_q, u"clicks"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/sponsored/click_reports"_q,
		tr::lng_myowngram_sponsored_click_reports(),
		SponsoredContent::SendClickReports,
		SponsoredContent::SetSendClickReports,
		{ u"ads"_q, u"click"_q, u"report"_q, u"tracking"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_sponsored_reporting_about());
}

void BuildDataSharingSection(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/data_sharing/usage_data"_q,
		.title = tr::lng_myowngram_usage_data(),
		.keywords = { u"usage"_q, u"data"_q, u"sharing"_q },
	});
	AddMyOwnGramToggle(
		builder,
		u"myowngram/data_sharing/channel_viewing"_q,
		tr::lng_myowngram_send_read_metrics(),
		ActivityReporting::SendReadMetrics,
		ActivityReporting::SetSendReadMetrics,
		{ u"channel"_q, u"viewing"_q, u"metrics"_q, u"data"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/data_sharing/music_listening"_q,
		tr::lng_myowngram_send_music_listen_reports(),
		ActivityReporting::SendMusicListenReports,
		ActivityReporting::SetSendMusicListenReports,
		{ u"music"_q, u"listening"_q, u"activity"_q, u"data"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_usage_data_about());

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/data_sharing/analytics_diagnostics"_q,
		.title = tr::lng_myowngram_analytics_diagnostics(),
		.keywords = { u"analytics"_q, u"diagnostics"_q, u"data"_q },
	});
	AddMyOwnGramToggle(
		builder,
		u"myowngram/data_sharing/premium_analytics"_q,
		tr::lng_myowngram_send_premium_promo_analytics(),
		ActivityReporting::SendPremiumPromoAnalytics,
		ActivityReporting::SetSendPremiumPromoAnalytics,
		{ u"premium"_q, u"promotion"_q, u"analytics"_q, u"data"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/data_sharing/call_diagnostics"_q,
		tr::lng_myowngram_upload_call_diagnostics(),
		ActivityReporting::UploadCallDiagnostics,
		ActivityReporting::SetUploadCallDiagnostics,
		{ u"call"_q, u"diagnostics"_q, u"upload"_q, u"data"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_analytics_diagnostics_about());

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/data_sharing/verification_messages"_q,
		.title = tr::lng_myowngram_verification_messages(),
		.keywords = { u"verification"_q, u"messages"_q, u"delivery"_q },
	});
	AddMyOwnGramToggle(
		builder,
		u"myowngram/data_sharing/verification_delivery"_q,
		tr::lng_myowngram_send_gateway_delivery_reports(),
		ActivityReporting::SendGatewayDeliveryReports,
		ActivityReporting::SetSendGatewayDeliveryReports,
		{ u"gateway"_q, u"verification"_q, u"delivery"_q, u"confirmation"_q });
	AddMyOwnGramGroupFooter(
		builder,
		tr::lng_myowngram_send_gateway_delivery_reports_about());
}

void BuildMyOwnGramMenu(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSectionButton({
		.title = tr::lng_myowngram_privacy(),
		.targetSection = MyOwnGramPrivacy::Id(),
		.icon = { &st::menuIconLock },
		.keywords = { u"privacy"_q, u"typing"_q, u"visibility"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_data_sharing(),
		.targetSection = MyOwnGramDataSharing::Id(),
		.icon = { &st::menuIconStats },
		.keywords = { u"data"_q, u"sharing"_q, u"analytics"_q, u"diagnostics"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_sponsored_content(),
		.targetSection = MyOwnGramSponsoredContent::Id(),
		.icon = { &st::menuIconEarn },
		.keywords = { u"ads"_q, u"sponsored"_q, u"tracking"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_general(),
		.targetSection = MyOwnGramGeneral::Id(),
		.icon = { &st::menuIconSettings },
		.keywords = { u"general"_q, u"logout"_q, u"preferences"_q },
	});
	builder.addSkip();
}

const auto kGeneralMeta = BuildHelper({
	.id = MyOwnGramGeneral::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_general,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	BuildGeneralSection(builder);
});

const auto kPrivacyMeta = BuildHelper({
	.id = MyOwnGramPrivacy::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_privacy,
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	BuildPrivacySection(builder);
});

const auto kDataSharingMeta = BuildHelper({
	.id = MyOwnGramDataSharing::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_data_sharing,
	.icon = &st::menuIconStats,
}, [](SectionBuilder &builder) {
	BuildDataSharingSection(builder);
});

const auto kSponsoredContentMeta = BuildHelper({
	.id = MyOwnGramSponsoredContent::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_sponsored_content,
	.icon = &st::menuIconEarn,
}, [](SectionBuilder &builder) {
	BuildSponsoredContentSection(builder);
});

const auto kMeta = BuildHelper({
	.id = MyOwnGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_myowngram_settings,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	BuildMyOwnGramMenu(builder);
});

const SectionBuildMethod kGeneralSection = kGeneralMeta.build;
const SectionBuildMethod kPrivacySection = kPrivacyMeta.build;
const SectionBuildMethod kDataSharingSection = kDataSharingMeta.build;
const SectionBuildMethod kSponsoredContentSection = kSponsoredContentMeta.build;
const SectionBuildMethod kMyOwnGramSection = kMeta.build;

MyOwnGram::MyOwnGram(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGram::title() {
	return tr::lng_myowngram_settings();
}

void MyOwnGram::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMyOwnGramSection);
	Ui::ResizeFitChild(this, content);
}

MyOwnGramGeneral::MyOwnGramGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGramGeneral::title() {
	return tr::lng_myowngram_general();
}

void MyOwnGramGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kGeneralSection);
	Ui::ResizeFitChild(this, content);
}

MyOwnGramPrivacy::MyOwnGramPrivacy(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGramPrivacy::title() {
	return tr::lng_myowngram_privacy();
}

void MyOwnGramPrivacy::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kPrivacySection);
	Ui::ResizeFitChild(this, content);
}

MyOwnGramDataSharing::MyOwnGramDataSharing(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGramDataSharing::title() {
	return tr::lng_myowngram_data_sharing();
}

void MyOwnGramDataSharing::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kDataSharingSection);
	Ui::ResizeFitChild(this, content);
}

MyOwnGramSponsoredContent::MyOwnGramSponsoredContent(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGramSponsoredContent::title() {
	return tr::lng_myowngram_sponsored_content();
}

void MyOwnGramSponsoredContent::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kSponsoredContentSection);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
