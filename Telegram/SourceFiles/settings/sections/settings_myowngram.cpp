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
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"

#include <tuple>

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

class Ads final : public Section<Ads> {
public:
	Ads(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

class Privacy final : public Section<Privacy> {
public:
	Privacy(QWidget *parent, not_null<Window::SessionController*> controller);

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
					return value.*flag;
				}),
		});
		if (button) {
			button->toggledValue() | rpl::on_next([=](bool enabled) {
				auto value = settings->adSettings(placement);
				value.*flag = enabled;
				settings->setAdSettings(placement, value);
			}, button->lifetime());
		}
	}
}

const auto kMain = BuildHelper({
	.id = MyOwnGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_myowngram_title,
	.icon = &st::menuIconCustomize,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSectionButton({
		.title = tr::lng_myowngram_ads_title(),
		.targetSection = Ads::Id(),
		.icon = { &st::menuIconBlock },
		.keywords = { u"ads"_q, u"sponsored"_q, u"advertising"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_privacy_title(),
		.targetSection = Privacy::Id(),
		.icon = { &st::menuIconLock },
		.keywords = { u"analytics"_q, u"tracking"_q, u"telemetry"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::lng_myowngram_about());
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
	builder.addDividerText(tr::lng_myowngram_ads_show_about());
	builder.addSkip();
	builder.addSubsectionTitle(tr::lng_myowngram_ads_get());
	AddAdToggles(builder, &Core::AdSettings::get);
	builder.addSkip();
	builder.addDividerText(tr::lng_myowngram_ads_get_about());
});

const auto kPrivacy = BuildHelper({
	.id = Privacy::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_privacy_title,
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	const auto settings = &Core::App().settings();
	builder.addSkip();
	const auto button = builder.addButton({
		.id = u"myowngram/privacy/reading-analytics"_q,
		.title = tr::lng_myowngram_reading_analytics(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->sendReadMetricsValue(),
		.keywords = { u"tracking"_q, u"telemetry"_q, u"read metrics"_q },
	});
	if (button) {
		button->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setSendReadMetrics(enabled);
		}, button->lifetime());
	}
	builder.addSkip();
	builder.addDividerText(tr::lng_myowngram_reading_analytics_about());
	builder.addSkip();
	const auto premium = builder.addButton({
		.id = u"myowngram/privacy/premium-promo-analytics"_q,
		.title = tr::lng_myowngram_premium_promo_analytics(),
		.st = &st::settingsButtonNoIcon,
		.toggled = settings->sendPremiumPromoAnalyticsValue(),
		.keywords = { u"tracking"_q, u"telemetry"_q, u"subscription"_q },
	});
	if (premium) {
		premium->toggledValue() | rpl::on_next([=](bool enabled) {
			settings->setSendPremiumPromoAnalytics(enabled);
		}, premium->lifetime());
	}
	builder.addSkip();
	builder.addDividerText(tr::lng_myowngram_premium_promo_analytics_about());
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

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
