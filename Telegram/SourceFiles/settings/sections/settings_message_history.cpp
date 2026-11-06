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
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {

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
		.title = tr::lng_myowngram_message_history_cleanup(),
		.options = options,
		.initialSelection = int(selected - periods.begin()),
		.callback = [=](int index) { archive->setRetention(periods[index]); },
	});
}

void MessageHistorySettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto archive = &controller->session().messageHistory();
	box->setTitle(tr::lng_myowngram_message_history_title());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto enabled = box->addRow(object_ptr<Ui::Checkbox>(
		box,
		tr::lng_myowngram_message_history_save(tr::now),
		archive->enabled()));
	enabled->checkedChanges() | rpl::on_next([=](bool value) {
		archive->setEnabled(value);
	}, enabled->lifetime());
	archive->enabledValue() | rpl::on_next([=](bool value) {
		enabled->setChecked(value);
	}, enabled->lifetime());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_message_history_about(),
		st::boxLabel));
	const auto autoDelete = box->addRow(object_ptr<Ui::Checkbox>(
		box,
		tr::lng_myowngram_message_history_auto_delete(tr::now),
		archive->includeAutoDelete()));
	autoDelete->checkedChanges() | rpl::on_next([=](bool value) {
		archive->setIncludeAutoDelete(value);
	}, autoDelete->lifetime());
	archive->includeAutoDeleteValue() | rpl::on_next([=](bool value) {
		autoDelete->setChecked(value);
	}, autoDelete->lifetime());
	box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		tr::lng_myowngram_message_history_download(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> choice) {
			SingleChoiceBox(choice, {
				.title = tr::lng_myowngram_message_history_download(),
				.options = {
					tr::lng_myowngram_message_history_download_off(tr::now),
					tr::lng_myowngram_message_history_download_automatic(tr::now),
					tr::lng_myowngram_message_history_download_always(tr::now),
				},
				.initialSelection = int(archive->download()),
				.callback = [=](int index) {
					archive->setDownload(Data::MessageHistory::Download(index));
				},
			});
		}));
	});
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_message_history_download_about(),
		st::boxLabel));
	box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		tr::lng_myowngram_message_history_cleanup(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		controller->show(Box(MessageHistoryCleanupBox, controller));
	});
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_message_history_cleanup_about(),
		st::boxLabel));
}

} // namespace Settings
