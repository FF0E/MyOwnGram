/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <vector>

class DocumentData;
class HistoryItem;
class PhotoData;

namespace HistoryView {

struct MessageDetail {
	QString label;
	QString value;
	QString copy;
	std::vector<MessageDetail> children;
};

[[nodiscard]] std::vector<MessageDetail> CollectMessageMetadata(
	not_null<HistoryItem*> item,
	PhotoData *selectedPhoto,
	DocumentData *selectedDocument);

} // namespace HistoryView
