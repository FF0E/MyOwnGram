/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_message_history.h"

#include "data/components/message_history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_myowngram.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Download = Data::MessageHistory::Download;

class MessageHistory final : public Section<MessageHistory> {
public:
	MessageHistory(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

rpl::producer<QString> RetentionLabel(int period) {
	if (!period) {
		return tr::lng_local_storage_limit_never();
	} else if (period < 0) {
		return tr::lng_weeks(lt_count, rpl::single(-period) | tr::to_count());
	}
	return tr::lng_months(lt_count, rpl::single(period) | tr::to_count());
}

rpl::producer<QString> DownloadLabel(Download value) {
	if (value == Download::Off) {
		return tr::lng_myowngram_message_history_download_off();
	} else if (value == Download::Automatic) {
		return tr::lng_myowngram_message_history_download_automatic();
	}
	return tr::lng_myowngram_message_history_download_always();
}

void DownloadBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto archive = &controller->session().messageHistory();
	SingleChoiceBox(box, {
		.title = tr::lng_myowngram_message_history_download(),
		.options = {
			tr::lng_myowngram_message_history_download_off(tr::now),
			tr::lng_myowngram_message_history_download_automatic(tr::now),
			tr::lng_myowngram_message_history_download_always(tr::now),
		},
		.initialSelection = int(archive->download()),
		.callback = [=](int index) { archive->setDownload(Download(index)); },
	});
}

const auto kMessageHistory = Builder::BuildHelper({
	.id = MessageHistory::Id(),
	.parentId = MyOwnGramId(),
	.title = &tr::lng_myowngram_message_history_title,
	.icon = &st::menuIconGroupLog,
}, [](Builder::SectionBuilder &builder) {
	const auto archive = &builder.session()->messageHistory();
	const auto controller = builder.controller();
	builder.addSkip();
	const auto edits = builder.addButton({
		.id = u"myowngram/message-history/edits"_q,
		.title = tr::lng_myowngram_message_history_save_edits(),
		.st = &st::settingsButtonNoIcon,
		.toggled = archive->saveEditsValue(),
		.keywords = { u"edited"_q, u"history"_q },
	});
	if (edits) {
		edits->toggledValue() | rpl::on_next([=](bool value) {
			archive->setSaveEdits(value);
		}, edits->lifetime());
	}
	const auto deleted = builder.addButton({
		.id = u"myowngram/message-history/deleted"_q,
		.title = tr::lng_myowngram_message_history_save_deleted(),
		.st = &st::settingsButtonNoIcon,
		.toggled = archive->saveDeletedValue(),
		.keywords = { u"deleted"_q, u"history"_q },
	});
	if (deleted) {
		deleted->toggledValue() | rpl::on_next([=](bool value) {
			archive->setSaveDeleted(value);
		}, deleted->lifetime());
	}
	builder.addDividerText(tr::lng_myowngram_message_history_about());
	builder.addSkip();
	const auto autoDelete = builder.addButton({
		.id = u"myowngram/message-history/auto-delete"_q,
		.title = tr::lng_myowngram_message_history_auto_delete(),
		.st = &st::settingsButtonNoIcon,
		.toggled = archive->includeAutoDeleteValue(),
		.keywords = { u"automatic"_q, u"deleted"_q, u"history"_q },
	});
	if (autoDelete) {
		autoDelete->toggledValue() | rpl::on_next([=](bool value) {
			archive->setIncludeAutoDelete(value);
		}, autoDelete->lifetime());
		archive->enabledValue() | rpl::on_next([=](bool value) {
			autoDelete->setDisabled(!value);
		}, autoDelete->lifetime());
	}
	const auto download = builder.addButton({
		.id = u"myowngram/message-history/download"_q,
		.title = tr::lng_myowngram_message_history_download(),
		.st = &st::settingsButtonNoIcon,
		.label = archive->downloadValue()
			| rpl::map(DownloadLabel)
			| rpl::flatten_latest(),
		.onClick = [=] { controller->show(Box(DownloadBox, controller)); },
		.keywords = { u"attachments"_q, u"media"_q, u"history"_q },
	});
	if (download) {
		archive->saveDeletedValue() | rpl::on_next([=](bool value) {
			download->setDisabled(!value);
		}, download->lifetime());
	}
	builder.addDividerText(tr::lng_myowngram_message_history_download_about());
	builder.addSkip();
	builder.addButton({
		.id = u"myowngram/message-history/cleanup"_q,
		.title = tr::lng_myowngram_message_history_cleanup(),
		.st = &st::settingsButtonNoIcon,
		.label = archive->retentionValue()
			| rpl::map(RetentionLabel)
			| rpl::flatten_latest(),
		.onClick = [=] {
			controller->show(Box(MessageHistoryCleanupBox, controller));
		},
		.keywords = { u"retention"_q, u"remove"_q, u"history"_q },
	});
	builder.addDividerText(tr::lng_myowngram_message_history_cleanup_about());
});

MessageHistory::MessageHistory(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMessageHistory.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> MessageHistory::title() {
	return tr::lng_myowngram_message_history_title();
}

} // namespace

Type MessageHistoryId() {
	return MessageHistory::Id();
}

void MessageHistoryCleanupBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto archive = &controller->session().messageHistory();
	auto periods = std::vector<int>{ 0, -1, -2, -3 };
	auto options = std::vector<QString>{
		tr::lng_local_storage_limit_never(tr::now),
	};
	for (auto weeks = 1; weeks <= 3; ++weeks) {
		options.push_back(tr::lng_weeks(tr::now, lt_count, weeks));
	}
	for (auto months = 1; months <= 12; ++months) {
		periods.push_back(months);
		options.push_back(tr::lng_months(tr::now, lt_count, months));
	}
	const auto selected = ranges::find(periods, archive->retention());
	SingleChoiceBox(box, {
		.title = tr::lng_myowngram_message_history_cleanup_title(),
		.options = options,
		.initialSelection = int(selected - periods.begin()),
		.callback = [=](int index) { archive->setRetention(periods[index]); },
	});
}

} // namespace Settings
