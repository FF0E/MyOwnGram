/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>

class HistoryItem;

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {

void AddMessageDetailsAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	std::shared_ptr<Main::SessionShow> show);

} // namespace HistoryView
