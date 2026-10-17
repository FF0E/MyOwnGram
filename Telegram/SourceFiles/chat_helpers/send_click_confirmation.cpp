/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/send_click_confirmation.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "ui/toast/toast.h"

namespace ChatHelpers {
namespace {

constexpr auto kConfirmationWindow = crl::time(2500);

} // namespace

SendClickConfirmation::~SendClickConfirmation() {
	clear();
}

bool SendClickConfirmation::confirm(
		not_null<Show*> show,
		const void *item,
		Type type) {
	const auto &settings = Core::App().settings();
	const auto sticker = (type == Type::Sticker);
	const auto enabled = sticker
		? settings.clickTwiceToSendStickers()
		: settings.clickTwiceToSendGifs();
	if (!enabled) {
		clear();
		return true;
	}
	const auto now = crl::now();
	const auto confirmed = (_item == item) && (now < _until);
	clear();
	if (confirmed) {
		return true;
	}
	_item = item;
	_until = now + kConfirmationWindow;
	_toast = show->showToast(sticker
		? tr::lng_myowngram_click_again_sticker(tr::now)
		: tr::lng_myowngram_click_again_gif(tr::now));
	return false;
}

void SendClickConfirmation::clear() {
	_item = nullptr;
	_until = 0;
	if (const auto toast = base::take(_toast).get()) {
		toast->hide();
	}
}

} // namespace ChatHelpers
