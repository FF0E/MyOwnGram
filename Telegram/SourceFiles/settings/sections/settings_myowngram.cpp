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
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"

#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

namespace ActivityReporting = ::MyOwnGram::ActivityReporting;

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

class MyOwnGramReporting final : public Section<MyOwnGramReporting> {
public:
	MyOwnGramReporting(
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
		rpl::producer<QString> about,
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
	builder.addSkip();
	builder.addDividerText(std::move(about));
}

void BuildGeneralSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addSkip();
	AddMyOwnGramToggle(
		builder,
		u"myowngram/keep_preferences_on_last_logout"_q,
		tr::lng_myowngram_keep_preferences_on_last_logout(),
		tr::lng_myowngram_keep_preferences_on_last_logout_about(),
		[=] { return settings->keepPreferencesOnLastLogout(); },
		[=](bool enabled) {
			settings->setKeepPreferencesOnLastLogout(enabled);
		},
		{ u"logout"_q, u"preferences"_q, u"settings"_q });
}

void BuildActivityReportingSection(SectionBuilder &builder) {
	builder.addSkip();
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/send_typing_status"_q,
		tr::lng_myowngram_send_typing_status(),
		tr::lng_myowngram_send_typing_status_about(),
		ActivityReporting::SendTypingStatus,
		ActivityReporting::SetSendTypingStatus,
		{ u"typing"_q, u"status"_q, u"activity"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/send_read_metrics"_q,
		tr::lng_myowngram_send_read_metrics(),
		tr::lng_myowngram_send_read_metrics_about(),
		ActivityReporting::SendReadMetrics,
		ActivityReporting::SetSendReadMetrics,
		{ u"view"_q, u"metrics"_q, u"activity"_q, u"privacy"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/send_music_listen_reports"_q,
		tr::lng_myowngram_send_music_listen_reports(),
		tr::lng_myowngram_send_music_listen_reports_about(),
		ActivityReporting::SendMusicListenReports,
		ActivityReporting::SetSendMusicListenReports,
		{ u"music"_q, u"listening"_q, u"activity"_q, u"privacy"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/send_premium_promo_analytics"_q,
		tr::lng_myowngram_send_premium_promo_analytics(),
		tr::lng_myowngram_send_premium_promo_analytics_about(),
		ActivityReporting::SendPremiumPromoAnalytics,
		ActivityReporting::SetSendPremiumPromoAnalytics,
		{ u"premium"_q, u"analytics"_q, u"activity"_q, u"privacy"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/upload_call_diagnostics"_q,
		tr::lng_myowngram_upload_call_diagnostics(),
		tr::lng_myowngram_upload_call_diagnostics_about(),
		ActivityReporting::UploadCallDiagnostics,
		ActivityReporting::SetUploadCallDiagnostics,
		{ u"call"_q, u"diagnostics"_q, u"activity"_q, u"privacy"_q });
	AddMyOwnGramToggle(
		builder,
		u"myowngram/activity_reporting/send_gateway_delivery_reports"_q,
		tr::lng_myowngram_send_gateway_delivery_reports(),
		tr::lng_myowngram_send_gateway_delivery_reports_about(),
		ActivityReporting::SendGatewayDeliveryReports,
		ActivityReporting::SetSendGatewayDeliveryReports,
		{ u"gateway"_q, u"delivery"_q, u"activity"_q, u"privacy"_q });
}

void BuildMyOwnGramMenu(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/categories"_q,
		.title = tr::lng_myowngram_categories(),
		.keywords = { u"categories"_q, u"settings"_q },
	});

	builder.addSectionButton({
		.title = tr::lng_myowngram_general(),
		.targetSection = MyOwnGramGeneral::Id(),
		.icon = { &st::menuIconSettings },
		.keywords = { u"general"_q, u"logout"_q, u"preferences"_q },
	});
	builder.addSectionButton({
		.title = tr::lng_myowngram_activity_reporting(),
		.targetSection = MyOwnGramReporting::Id(),
		.icon = { &st::menuIconStats },
		.keywords = { u"activity"_q, u"reporting"_q, u"privacy"_q },
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

const auto kActivityReportingMeta = BuildHelper({
	.id = MyOwnGramReporting::Id(),
	.parentId = MyOwnGram::Id(),
	.title = &tr::lng_myowngram_activity_reporting,
	.icon = &st::menuIconStats,
}, [](SectionBuilder &builder) {
	BuildActivityReportingSection(builder);
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
const SectionBuildMethod kActivityReportingSection
	= kActivityReportingMeta.build;
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

MyOwnGramReporting::MyOwnGramReporting(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MyOwnGramReporting::title() {
	return tr::lng_myowngram_activity_reporting();
}

void MyOwnGramReporting::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kActivityReportingSection);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
