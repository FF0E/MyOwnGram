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

void BuildGeneralSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

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

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
