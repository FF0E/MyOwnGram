/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;
class PhotoData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace HistoryView {

void AddMessageDetailsAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	PhotoData *selectedPhoto = nullptr,
	DocumentData *selectedDocument = nullptr);

} // namespace HistoryView
