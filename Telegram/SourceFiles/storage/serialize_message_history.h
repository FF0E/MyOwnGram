/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"

struct PollMedia;
struct WebPageData;

namespace Main {
class Session;
} // namespace Main

namespace Serialize {

class MessageHistory final {
public:
	using Item = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;

	[[nodiscard]] static QByteArray Write(not_null<HistoryItem*> item);
	[[nodiscard]] static Item Read(
		not_null<History*> history,
		const QByteArray &bytes,
		bool inlineCopy);

private:
	static void WritePhoto(QDataStream &stream, PhotoData *photo);
	static PhotoData *ReadPhoto(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion);
	static void WriteDocument(QDataStream &stream, DocumentData *document);
	static DocumentData *ReadDocument(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion);
	static void WriteWebPage(
		QDataStream &stream,
		not_null<WebPageData*> page,
		qint32 flags);
	static WebPageData *ReadWebPage(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion,
		qint32 &flags);
	static void WritePollMedia(QDataStream &stream, const PollMedia &media);
	[[nodiscard]] static PollMedia ReadPollMedia(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion);
	static void WriteRich(QDataStream &stream, not_null<HistoryItem*> item);
	[[nodiscard]] static std::shared_ptr<const Iv::RichPage> ReadRich(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion);
	static void WriteMedia(QDataStream &stream, Data::Media *media);
	[[nodiscard]] static std::unique_ptr<Data::Media> ReadMedia(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion,
		int format);

};

} // namespace Serialize
