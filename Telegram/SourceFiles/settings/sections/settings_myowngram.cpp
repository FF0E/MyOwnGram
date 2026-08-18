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
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_menu_icons.h"

namespace Settings {
namespace {

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

void AddActivityReportingCheckbox(
		SectionBuilder &builder,
		QString id,
		rpl::producer<QString> title,
		rpl::producer<QString> about,
		bool (*getter)(),
		void (*setter)(bool),
		QStringList keywords) {
	const auto checkbox = builder.addCheckbox({
		.id = std::move(id),
		.title = std::move(title),
		.checked = getter(),
		.keywords = std::move(keywords),
	});
	if (checkbox) {
		checkbox->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return checked != getter();
		}) | rpl::on_next([=](bool checked) {
			setter(checked);
		}, checkbox->lifetime());
	}
	builder.addSkip();
	builder.addDividerText(std::move(about));
}

void BuildGeneralSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/general"_q,
		.title = tr::lng_myowngram_general(),
		.keywords = { u"general"_q, u"logout"_q, u"preferences"_q },
	});

	const auto keepPreferences = builder.addCheckbox({
		.id = u"myowngram/keep_preferences_on_last_logout"_q,
		.title = tr::lng_myowngram_keep_preferences_on_last_logout(),
		.checked = settings->keepPreferencesOnLastLogout(),
		.keywords = { u"logout"_q, u"preferences"_q, u"settings"_q },
	});
	if (keepPreferences) {
		keepPreferences->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return checked != settings->keepPreferencesOnLastLogout();
		}) | rpl::on_next([=](bool checked) {
			settings->setKeepPreferencesOnLastLogout(checked);
		}, keepPreferences->lifetime());
	}

	builder.addSkip();
	builder.addDividerText(
		tr::lng_myowngram_keep_preferences_on_last_logout_about());
}

void BuildActivityReportingSection(SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"myowngram/activity_reporting"_q,
		.title = tr::lng_myowngram_activity_reporting(),
		.keywords = { u"activity"_q, u"reporting"_q, u"privacy"_q },
	});

	AddActivityReportingCheckbox(
		builder,
		u"myowngram/activity_reporting/send_typing_status"_q,
		tr::lng_myowngram_send_typing_status(),
		tr::lng_myowngram_send_typing_status_about(),
		ActivityReporting::SendTypingStatus,
		ActivityReporting::SetSendTypingStatus,
		{ u"typing"_q, u"status"_q, u"activity"_q });
	AddActivityReportingCheckbox(
		builder,
		u"myowngram/activity_reporting/send_read_metrics"_q,
		tr::lng_myowngram_send_read_metrics(),
		tr::lng_myowngram_send_read_metrics_about(),
		ActivityReporting::SendReadMetrics,
		ActivityReporting::SetSendReadMetrics,
		{ u"view"_q, u"metrics"_q, u"activity"_q, u"privacy"_q });
	AddActivityReportingCheckbox(
		builder,
		u"myowngram/activity_reporting/send_music_listen_reports"_q,
		tr::lng_myowngram_send_music_listen_reports(),
		tr::lng_myowngram_send_music_listen_reports_about(),
		ActivityReporting::SendMusicListenReports,
		ActivityReporting::SetSendMusicListenReports,
		{ u"music"_q, u"listening"_q, u"activity"_q, u"privacy"_q });
}

void BuildMyOwnGramSection(SectionBuilder &builder) {
	BuildGeneralSection(builder);
	BuildActivityReportingSection(builder);
}

const auto kMeta = BuildHelper({
	.id = MyOwnGram::Id(),
	.parentId = MainId(),
	.title = &tr::lng_myowngram_settings,
	.icon = &st::menuIconSettings,
}, [](SectionBuilder &builder) {
	BuildMyOwnGramSection(builder);
});

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

} // namespace

Type MyOwnGramId() {
	return MyOwnGram::Id();
}

} // namespace Settings
