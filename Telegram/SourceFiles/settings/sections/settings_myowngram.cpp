/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_myowngram.h"

#include "apiwrap.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <tuple>

#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

class MyOwnGram final : public Section<MyOwnGram> {
public:
	MyOwnGram(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

class ChatsAndProfiles final : public Section<ChatsAndProfiles> {
public:
	ChatsAndProfiles(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

class Privacy final : public Section<Privacy> {
public:
	Privacy(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

class ActivitySharing final : public Section<ActivitySharing> {
public:
	ActivitySharing(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

class Ads final : public Section<Ads> {
public:
	Ads(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

void AddAdToggles(
		SectionBuilder &builder,
		bool Core::AdSettings::*flag) {
	const auto settings = &Core::App().settings();
	const auto rows = std::array{
		std::tuple{
			Core::AdPlacement::Channel,
			&tr::lng_myowngram_ads_show_channel,
			&tr::lng_myowngram_ads_get_channel,
		},
		std::tuple{
			Core::AdPlacement::Bot,
			&tr::lng_myowngram_ads_show_bot,
			&tr::lng_myowngram_ads_get_bot,
		},
		std::tuple{
			Core::AdPlacement::Search,
			&tr::lng_myowngram_ads_show_search,
			&tr::lng_myowngram_ads_get_search,
		},
		std::tuple{
			Core::AdPlacement::Video,
			&tr::lng_myowngram_ads_show_video,
			&tr::lng_myowngram_ads_get_video,
		},
	};
	const auto show = (flag == &Core::AdSettings::show);
	const auto section = show ? u"show"_q : u"get"_q;
	for (const auto &[placement, showTitle, getTitle] : rows) {
		const auto title = show ? showTitle : getTitle;
		const auto button = builder.addButton({
			.id = u"myowngram/ads/%1/%2"_q.arg(section).arg(int(placement)),
			.title = (*title)(),
			.st = &st::settingsButtonNoIcon,
			.toggled = settings->adSettingsValue(placement)
				| rpl::map([=](Core::AdSettings value) {
					return !(value.*flag);
				}),
			.keywords = {
				u"sponsored"_q,
				u"advertising"_q,
				show ? u"show"_q : u"get"_q,
			},
		});
		if (button) {
			button->toggledValue() | rpl::on_next([=](bool enabled) {
				auto value = settings->adSettings(placement);
				value.*flag = !enabled;
				settings->setAdSettings(placement, value);
			}, button->lifetime());
		}
	}
}

void AddActivityToggles(SectionBuilder &builder) {
	struct Row {
		bool Core::ActivitySettings::*flag = nullptr;
		const tr::phrase<> *title = nullptr;
		QString id;
		const tr::phrase<> *about = nullptr;
		const tr::phrase<> *section = nullptr;
	};
	const auto rows = std::array{
		Row{
			&Core::ActivitySettings::online,
			&tr::lng_myowngram_send_online_status,
			u"online"_q,
			nullptr,
			&tr::lng_myowngram_chat_activity,
		},
		Row{
			&Core::ActivitySettings::typing,
			&tr::lng_myowngram_send_typing_status,
			u"typing"_q,
		},
		Row{
			&Core::ActivitySettings::recording,
			&tr::lng_myowngram_send_recording_status,
			u"recording"_q,
		},
		Row{
			&Core::ActivitySettings::uploading,
			&tr::lng_myowngram_send_upload_status,
			u"uploading"_q,
		},
		Row{
			&Core::ActivitySettings::stickerSelection,
			&tr::lng_myowngram_send_sticker_selection_status,
			u"stickers"_q,
		},
		Row{
			&Core::ActivitySettings::game,
			&tr::lng_myowngram_send_game_activity_status,
			u"games"_q,
		},
		Row{
			&Core::ActivitySettings::speaking,
			&tr::lng_myowngram_send_group_call_speaking_status,
			u"speaking"_q,
			nullptr,
			&tr::lng_myowngram_group_calls,
		},
		Row{
			&Core::ActivitySettings::emojiEffects,
			&tr::lng_myowngram_share_emoji_effects,
			u"emoji-effects"_q,
			&tr::lng_myowngram_share_emoji_effects_about,
			&tr::lng_myowngram_emoji_effects,
		},
		Row{
			&Core::ActivitySettings::emojiWatching,
			&tr::lng_myowngram_send_emoji_watching_status,
			u"emoji-watching"_q,
			&tr::lng_myowngram_emoji_watching_about,
		},
	};
	const auto settings = &Core::App().settings();
	for (const auto &row : rows) {
		const auto flag = row.flag;
		if (row.section) {
			if (flag != &Core::ActivitySettings::online) {
				builder.addSkip();
				builder.addDivider();
				builder.addSkip();
			}
			builder.addSubsectionTitle((*row.section)());
		}
		const auto button = builder.addButton({
			.id = u"myowngram/privacy/activity/"_q + row.id,
			.title = (*row.title)(),
			.description = row.about ? (*row.about)() : nullptr,
			.st = &st::settingsButtonNoIcon,
			.toggled = settings->activitySettingsValue()
				| rpl::map([=](const Core::ActivitySettings &value) {
					return !(value.*flag);
				}) | rpl::distinct_until_changed(),
			.keywords = { u"activity"_q, u"status"_q },
		});
		if (button) {
			button->toggledValue() | rpl::on_next([=](bool enabled) {
				auto value = settings->activitySettings();
				value.*flag = !enabled;
				settings->setActivitySettings(value);
			}, button->lifetime());
		}
	}
}

void ConfirmClearCloudDrafts(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_myowngram_clear_cloud_drafts_sure(),
		.confirmed = crl::guard(controller, [=](Fn<void()> close) {
			close();
			controller->session().api().request(MTPmessages_ClearAllDrafts(
			)).done(crl::guard(controller, [=] {
				controller->showToast(
					tr::lng_myowngram_cloud_drafts_cleared(tr::now));
			})).fail(crl::guard(controller, [=](const MTP::Error &error) {
				MTP::ShowErrorFallback(controller, error);
			})).send();
		}),
		.confirmText = tr::lng_recent_clear_all(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_myowngram_clear_cloud_drafts(),
	}));
}

void ConfirmClearLocalDrafts(not_null<Window::SessionController*> controller) {
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_myowngram_clear_local_drafts_sure(),
		.confirmed = crl::guard(controller, [=](Fn<void()> close) {
			close();
			controller->session().local().clearDrafts();
			controller->showToast(
				tr::lng_myowngram_local_drafts_cleared(tr::now));
		}),
		.confirmText = tr::lng_recent_clear_all(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_myowngram_clear_local_drafts(),
	}));
}

void PeerIdBox(not_null<Ui::GenericBox*> box) {
	using Mode = Core::PeerIdDisplay;
	const auto settings = &Core::App().settings();
	const auto group = std::make_shared<Ui::RadioenumGroup<Mode>>(
		settings->peerIdDisplay());
	box->setTitle(tr::lng_myowngram_peer_ids());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_peer_ids_about(),
		st::boxLabel));
	struct Choice {
		Mode value = Mode::Hidden;
		const tr::phrase<> *title = nullptr;
		const tr::phrase<> *about = nullptr;
	};
	const auto choices = std::array{
		Choice{ Mode::Hidden, &tr::lng_myowngram_peer_ids_hidden },
		Choice{
			Mode::TelegramApi,
			&tr::lng_myowngram_peer_ids_telegram,
			&tr::lng_myowngram_peer_ids_telegram_about,
		},
		Choice{
			Mode::BotApi,
			&tr::lng_myowngram_peer_ids_bot,
			&tr::lng_myowngram_peer_ids_bot_about,
		},
	};
	for (const auto &choice : choices) {
		box->addSkip(st::boxOptionListSkip);
		box->addRow(object_ptr<Ui::Radioenum<Mode>>(
			box,
			group,
			choice.value,
			(*choice.title)(tr::now),
			st::defaultBoxCheckbox));
		if (choice.about) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					(*choice.about)(),
					st::settingsExperimentalAbout),
				QMargins(
					st::boxRowPadding.left()
						+ st::defaultBoxCheckbox.checkPosition.x()
						+ st::defaultRadio.diameter
						+ st::defaultBoxCheckbox.textPosition.x(),
					0,
					st::boxRowPadding.right(),
					0));
		}
	}
	group->setChangedCallback([=](Mode value) {
		settings->setPeerIdDisplay(value);
		box->closeBox();
	});
}

void AddPeerIdSelector(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addButton({
		.id = u"myowngram/peer-ids"_q,
		.title = tr::lng_myowngram_peer_ids(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::combine(
			settings->peerIdDisplayValue(),
			tr::lng_myowngram_peer_ids_hidden(),
			tr::lng_myowngram_peer_ids_telegram(),
			tr::lng_myowngram_peer_ids_bot()
		) | rpl::map([](
				Core::PeerIdDisplay value,
				const QString &hidden,
				const QString &telegram,
				const QString &bot) {
			switch (value) {
			case Core::PeerIdDisplay::Hidden: return hidden;
			case Core::PeerIdDisplay::TelegramApi: return telegram;
			case Core::PeerIdDisplay::BotApi: return bot;
			}
			Unexpected("Profile ID format.");
		}),
		.onClick = [controller = builder.controller()] {
			controller->show(Box(PeerIdBox));
		},
		.keywords = { u"uid"_q, u"dc"_q, u"profile"_q, u"datacenter"_q },
	});
}

const auto kMain = BuildHelper({
	.id = MyOwnGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_myowngram_title,
	.icon = &st::menuIconCustomize,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSectionButton({
		.title = tr::lng_myowngram_chats_profiles_title(),
		.targetSection = ChatsAndProfiles::Id(),
		.icon = { &st::menuIconChatBubble },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_privacy_title(),
		.targetSection = Privacy::Id(),
		.icon = { &st::menuIconLock },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_ads_title(),
		.targetSection = Ads::Id(),
		.icon = { &st::menuIconBlock },
	});
});

const auto kChatsAndProfiles = BuildHelper({
	.id = ChatsAndProfiles::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_chats_profiles_title,
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_settings_messages());
	const auto seconds = builder.addButton({
		.id = u"myowngram/message-seconds"_q,
		.title = tr::lng_myowngram_show_message_seconds(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->showMessageSecondsValue(),
		.keywords = { u"message"_q, u"time"_q, u"seconds"_q, u"timestamp"_q },
	});
	if (seconds) {
		seconds->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setShowMessageSeconds(enabled);
		}, seconds->lifetime());
	}
	const auto messageId = builder.addButton({
		.id = u"myowngram/message-id-tooltip"_q,
		.title = tr::lng_myowngram_show_message_id_in_tooltip(),
		.description = tr::lng_myowngram_message_id_tooltip_about(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->showMessageIdInTooltipValue(),
		.keywords = { u"message"_q, u"id"_q, u"date"_q, u"tooltip"_q },
	});
	if (messageId) {
		messageId->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setShowMessageIdInTooltip(enabled);
		}, messageId->lifetime());
	}
	const auto stickers = builder.addButton({
		.id = u"myowngram/click-twice-stickers"_q,
		.title = tr::lng_myowngram_click_twice_stickers(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->clickTwiceToSendStickersValue(),
		.keywords = { u"stickers"_q, u"send"_q, u"confirm"_q, u"click"_q },
	});
	if (stickers) {
		stickers->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setClickTwiceToSendStickers(enabled);
		}, stickers->lifetime());
	}
	const auto gifs = builder.addButton({
		.id = u"myowngram/click-twice-gifs"_q,
		.title = tr::lng_myowngram_click_twice_gifs(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->clickTwiceToSendGifsValue(),
		.keywords = { u"gifs"_q, u"send"_q, u"confirm"_q, u"click"_q },
	});
	if (gifs) {
		gifs->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setClickTwiceToSendGifs(enabled);
		}, gifs->lifetime());
	}
	const auto voicePreview = builder.addButton({
		.id = u"myowngram/preview-voice-messages"_q,
		.title = tr::lng_myowngram_preview_voice_messages(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->previewVoiceMessagesValue(),
		.keywords = { u"voice"_q, u"recording"_q, u"preview"_q, u"send"_q },
	});
	if (voicePreview) {
		voicePreview->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setPreviewVoiceMessages(enabled);
		}, voicePreview->lifetime());
	}
	const auto videoPreview = builder.addButton({
		.id = u"myowngram/preview-video-messages"_q,
		.title = tr::lng_myowngram_preview_video_messages(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->previewVideoMessagesValue(),
		.keywords = { u"video"_q, u"round"_q, u"recording"_q, u"preview"_q },
	});
	if (videoPreview) {
		videoPreview->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setPreviewVideoMessages(enabled);
		}, videoPreview->lifetime());
	}
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_profiles_title());
	AddPeerIdSelector(builder);
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_drafts_title());
	const auto sendDrafts = builder.addButton({
		.id = u"myowngram/privacy/send-drafts"_q,
		.title = tr::lng_myowngram_send_drafts(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->sendDraftsValue()
			| rpl::map(!rpl::mappers::_1),
		.keywords = { u"drafts"_q, u"sync"_q, u"cloud"_q },
	});
	if (sendDrafts) {
		sendDrafts->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setSendDrafts(!enabled);
		}, sendDrafts->lifetime());
	}
	const auto receiveDrafts = builder.addButton({
		.id = u"myowngram/privacy/receive-drafts"_q,
		.title = tr::lng_myowngram_receive_drafts(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->receiveDraftsValue()
			| rpl::map(!rpl::mappers::_1),
		.keywords = { u"drafts"_q, u"sync"_q, u"cloud"_q },
	});
	if (receiveDrafts) {
		receiveDrafts->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setReceiveDrafts(!enabled);
		}, receiveDrafts->lifetime());
	}
	builder.addButton({
		.id = u"myowngram/clear-cloud-drafts"_q,
		.title = tr::lng_myowngram_clear_cloud_drafts(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [controller = builder.controller()] {
			ConfirmClearCloudDrafts(controller);
		},
		.keywords = { u"drafts"_q, u"clear"_q, u"cloud"_q },
	});
	builder.addButton({
		.id = u"myowngram/clear-local-drafts"_q,
		.title = tr::lng_myowngram_clear_local_drafts(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [controller = builder.controller()] {
			ConfirmClearLocalDrafts(controller);
		},
		.keywords = { u"drafts"_q, u"clear"_q, u"local"_q },
	});
});

const auto kPrivacy = BuildHelper({
	.id = Privacy::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_privacy_title,
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();
	builder.addSectionButton({
		.title = tr::lng_myowngram_activity_title(),
		.targetSection = ActivitySharing::Id(),
		.icon = { &st::menuIconChatDiscuss },
	});
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_links_title());
	const auto botLinks = builder.addButton({
		.id = u"myowngram/privacy/confirm-bot-links"_q,
		.title = tr::lng_myowngram_confirm_bot_links(),
		.description = tr::lng_myowngram_confirm_bot_links_about(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->confirmBotLinksValue(),
		.keywords = { u"links"_q, u"bots"_q, u"parameters"_q, u"referrals"_q },
	});
	if (botLinks) {
		botLinks->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setConfirmBotLinks(enabled);
		}, botLinks->lifetime());
	}
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_usage_reports_title());
	const auto button = builder.addButton({
		.id = u"myowngram/privacy/reading-analytics"_q,
		.title = tr::lng_myowngram_reading_analytics(),
		.description = tr::lng_myowngram_reading_analytics_about(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->sendReadMetricsValue()
			| rpl::map(!rpl::mappers::_1),
		.keywords = {
			u"analytics"_q,
			u"tracking"_q,
			u"telemetry"_q,
			u"read metrics"_q,
		},
	});
	if (button) {
		button->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setSendReadMetrics(!enabled);
		}, button->lifetime());
	}
	const auto premium = builder.addButton({
		.id = u"myowngram/privacy/premium-promo-analytics"_q,
		.title = tr::lng_myowngram_premium_promo_analytics(),
		.description = tr::lng_myowngram_premium_promo_analytics_about(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->sendPremiumPromoAnalyticsValue()
			| rpl::map(!rpl::mappers::_1),
		.keywords = {
			u"analytics"_q,
			u"tracking"_q,
			u"telemetry"_q,
			u"subscription"_q,
		},
	});
	if (premium) {
		premium->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setSendPremiumPromoAnalytics(!enabled);
		}, premium->lifetime());
	}
});

const auto kActivitySharing = BuildHelper({
	.id = ActivitySharing::Id(),
	.parentId = Privacy::Id(),
	.title = &tr::lng_myowngram_activity_title,
	.icon = &st::menuIconChatDiscuss,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	AddActivityToggles(builder);
});

const auto kAds = BuildHelper({
	.id = Ads::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_ads_title,
	.icon = &st::menuIconBlock,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_ads_show());
	AddAdToggles(builder, &Core::AdSettings::show);
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_ads_get());
	builder.add([](const WidgetContext &ctx) {
		return SectionBuilder::WidgetToAdd{
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				tr::lng_myowngram_ads_get_about(),
				st::settingsExperimentalAbout),
			.margin = st::settingsExperimentalAboutPadding,
		};
	});
	AddAdToggles(builder, &Core::AdSettings::get);
});

MyOwnGram::MyOwnGram(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMain.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> MyOwnGram::title() {
	return tr::lng_myowngram_title();
}

ChatsAndProfiles::ChatsAndProfiles(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kChatsAndProfiles.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> ChatsAndProfiles::title() {
	return tr::lng_myowngram_chats_profiles_title();
}

Privacy::Privacy(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kPrivacy.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> Privacy::title() {
	return tr::lng_myowngram_privacy_title();
}

ActivitySharing::ActivitySharing(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kActivitySharing.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> ActivitySharing::title() {
	return tr::lng_myowngram_activity_title();
}

Ads::Ads(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kAds.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> Ads::title() {
	return tr::lng_myowngram_ads_title();
}

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
