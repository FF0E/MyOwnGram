// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_snapshot.h"

#include "history/history_item.h"
#include "history/history_item_components.h"
#include "myowngram/message_archive_deletion.h"
#include "myowngram/message_archive_engagement.h"
#include "myowngram/message_archive_markup.h"
#include "myowngram/message_archive_media.h"

#include <QtCore/QDataStream>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kMessageMediaVisibleVersion = uint16(1);
constexpr auto kNestedMediaVersion = uint16(1);
constexpr auto kMessageSnapshotSupportVersion = uint16(1);

struct SerializedMessageMedia {
	MessageMediaType type = MessageMediaType::None;
	QByteArray visible;
	QByteArray support;
};

std::optional<SerializedMessageMedia> SerializeMessageMedia(
		const Data::Media *media) {
	if (!media) {
		return SerializedMessageMedia();
	} else if (const auto photo = SerializePhotoMedia(media)) {
		return SerializedMessageMedia{
			.type = MessageMediaType::Photo,
			.visible = photo->visible,
			.support = photo->support,
		};
	} else if (const auto document = SerializeDocumentMedia(media)) {
		return SerializedMessageMedia{
			.type = MessageMediaType::Document,
			.visible = document->visible,
			.support = document->support,
		};
	}
	return std::nullopt;
}

std::optional<RecordType> MediaRecordType(MessageMediaType type) {
	switch (type) {
	case MessageMediaType::Photo:
		return RecordType::PhotoMediaVisible;
	case MessageMediaType::Document:
		return RecordType::DocumentMediaVisible;
	case MessageMediaType::None:
		return std::nullopt;
	}
	return std::nullopt;
}

bool GoodMediaRecord(const MessageMediaVisible &media) {
	const auto type = MediaRecordType(media.type);
	return type
		&& ParseRecord(
			media.record,
			*type,
			kNestedMediaVersion);
}

ParsedMessageMediaVisible ParseMediaFailure(ParseError error) {
	return { .error = error };
}

ParsedMessageSnapshotSupport ParseSupportFailure(ParseError error) {
	return { .error = error };
}

#ifdef _DEBUG
void CheckMessageSnapshotFormat() {
	const auto photo = SerializeRecord(
		RecordType::PhotoMediaVisible,
		1,
		QByteArray("photo"));
	Assert(photo.has_value());
	const auto media = MessageMediaVisible{
		.type = MessageMediaType::Photo,
		.invertMedia = true,
		.record = *photo,
	};
	const auto serializedMedia = SerializeMessageMediaVisible(media);
	Assert(serializedMedia.has_value());
	const auto parsedMedia = ParseMessageMediaVisible(*serializedMedia);
	Assert(parsedMedia && parsedMedia.value == media);
	const auto unsupportedPhoto = SerializeRecord(
		RecordType::PhotoMediaVisible,
		kNestedMediaVersion + 1,
		QByteArray("photo"));
	Assert(unsupportedPhoto.has_value());
	Assert(!SerializeMessageMediaVisible({
		.type = MessageMediaType::Photo,
		.record = *unsupportedPhoto,
	}));
	Assert(!SerializeMessageMediaVisible({
		.type = MessageMediaType::None,
		.invertMedia = true,
	}));
	Assert(ParseMessageMediaVisible(QByteArray()));

	const auto deletion = SerializeDeletedMessageContext({
		.from = peerFromUser(UserId(1)),
		.date = 2,
	});
	Assert(deletion.has_value());
	const auto engagement = SerializeDeletedMessageEngagement({
		.views = DeletedMessageViews{ .views = 3 },
	});
	Assert(engagement.has_value());
	const auto support = MessageSnapshotSupport{
		.media = QByteArray("media support"),
		.replyMarkup = QByteArray("markup support"),
		.deletion = *deletion,
		.engagement = *engagement,
	};
	const auto serializedSupport = SerializeMessageSnapshotSupport(support);
	Assert(serializedSupport.has_value());
	const auto supportRecord = ParseRecord(
		*serializedSupport,
		RecordType::MessageSnapshotSupport,
		kMessageSnapshotSupportVersion);
	Assert(supportRecord
		&& supportRecord.version == kMessageSnapshotSupportVersion);
	const auto parsedSupport = ParseMessageSnapshotSupport(*serializedSupport);
	Assert(parsedSupport && parsedSupport.value == support);
	Assert(ParseMessageSnapshotSupport(QByteArray()));

	auto editSupport = support;
	editSupport.deletion.clear();
	editSupport.engagement.clear();
	const auto serializedEditSupport = SerializeMessageSnapshotSupport(
		editSupport);
	Assert(serializedEditSupport.has_value());
	const auto parsedEditSupport = ParseMessageSnapshotSupport(
		*serializedEditSupport);
	Assert(parsedEditSupport && parsedEditSupport.value == editSupport);

	auto deletionSupport = support;
	deletionSupport.engagement.clear();
	const auto serializedDeletionSupport = SerializeMessageSnapshotSupport(
		deletionSupport);
	Assert(serializedDeletionSupport.has_value());
	const auto parsedDeletionSupport = ParseMessageSnapshotSupport(
		*serializedDeletionSupport);
	Assert(parsedDeletionSupport
		&& parsedDeletionSupport.value == deletionSupport);

	auto incompletePayload = QByteArray();
	auto incompleteStream = QDataStream(
		&incompletePayload,
		QIODevice::WriteOnly);
	incompleteStream.setVersion(QDataStream::Qt_5_1);
	incompleteStream.setByteOrder(QDataStream::BigEndian);
	Assert(Binary::WriteBytes(incompleteStream, support.media));
	Assert(Binary::WriteBytes(incompleteStream, support.replyMarkup));
	const auto incompleteSupport = SerializeRecord(
		RecordType::MessageSnapshotSupport,
		kMessageSnapshotSupportVersion,
		incompletePayload);
	Assert(incompleteSupport.has_value());
	Assert(!ParseMessageSnapshotSupport(*incompleteSupport));

	auto invalidSupport = support;
	invalidSupport.deletion.chop(1);
	Assert(!SerializeMessageSnapshotSupport(invalidSupport));
	invalidSupport = support;
	invalidSupport.engagement.chop(1);
	Assert(!SerializeMessageSnapshotSupport(invalidSupport));
	invalidSupport = support;
	invalidSupport.deletion.clear();
	Assert(!SerializeMessageSnapshotSupport(invalidSupport));

	auto corrupt = *serializedMedia;
	corrupt.chop(1);
	Assert(!ParseMessageMediaVisible(corrupt));
	const auto mediaRecord = ParseRecord(
		*serializedMedia,
		RecordType::MessageMediaVisible,
		kMessageMediaVisibleVersion);
	Assert(mediaRecord);
	auto invalidPayload = mediaRecord.payload;
	invalidPayload[0] = char(255);
	const auto invalidType = SerializeRecord(
		RecordType::MessageMediaVisible,
		kMessageMediaVisibleVersion,
		invalidPayload);
	Assert(invalidType.has_value());
	Assert(!ParseMessageMediaVisible(*invalidType));
}
#endif // _DEBUG

} // namespace

std::optional<QByteArray> SerializeMessageMediaVisible(
		const MessageMediaVisible &media) {
	if (media.type == MessageMediaType::None) {
		return (!media.invertMedia && media.record.isEmpty())
			? std::optional<QByteArray>(QByteArray())
			: std::nullopt;
	} else if (!GoodMediaRecord(media)) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< quint8(media.type)
		<< quint8(media.invertMedia ? 1 : 0);
	if (!Binary::WriteBytes(stream, media.record)
		|| stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::MessageMediaVisible,
		kMessageMediaVisibleVersion,
		payload);
}

ParsedMessageMediaVisible ParseMessageMediaVisible(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {
			.value = MessageMediaVisible(),
			.error = ParseError::None,
		};
	}
	const auto record = ParseRecord(
		serialized,
		RecordType::MessageMediaVisible,
		kMessageMediaVisibleVersion);
	if (!record) {
		return ParseMediaFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto type = quint8();
	auto invertMedia = quint8();
	stream >> type >> invertMedia;
	const auto nested = Binary::ReadBytes(stream);
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| invertMedia > 1
		|| !nested) {
		return ParseMediaFailure(ParseError::Corrupt);
	}
	const auto result = MessageMediaVisible{
		.type = MessageMediaType(type),
		.invertMedia = (invertMedia != 0),
		.record = *nested,
	};
	if (!GoodMediaRecord(result)) {
		return ParseMediaFailure(ParseError::Corrupt);
	}
	return {
		.value = result,
		.error = ParseError::None,
	};
}

std::optional<QByteArray> SerializeMessageSnapshotSupport(
		const MessageSnapshotSupport &support) {
	if (support.media.isEmpty()
		&& support.replyMarkup.isEmpty()
		&& support.deletion.isEmpty()
		&& support.engagement.isEmpty()) {
		return QByteArray();
	} else if ((!support.deletion.isEmpty()
			&& !ParseDeletedMessageContext(support.deletion))
		|| (!support.engagement.isEmpty()
			&& (support.deletion.isEmpty()
				|| !ParseDeletedMessageEngagement(
					support.engagement)))) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	if (!Binary::WriteBytes(stream, support.media)
		|| !Binary::WriteBytes(stream, support.replyMarkup)
		|| !Binary::WriteBytes(stream, support.deletion)
		|| !Binary::WriteBytes(stream, support.engagement)
		|| stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::MessageSnapshotSupport,
		kMessageSnapshotSupportVersion,
		payload);
}

ParsedMessageSnapshotSupport ParseMessageSnapshotSupport(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {
			.value = MessageSnapshotSupport(),
			.error = ParseError::None,
		};
	}
	const auto record = ParseRecord(
		serialized,
		RecordType::MessageSnapshotSupport,
		kMessageSnapshotSupportVersion);
	if (!record) {
		return ParseSupportFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	const auto media = Binary::ReadBytes(stream);
	const auto replyMarkup = Binary::ReadBytes(stream);
	const auto deletion = Binary::ReadBytes(stream);
	const auto engagement = Binary::ReadBytes(stream);
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| !media
		|| !replyMarkup
		|| !deletion
		|| !engagement
		|| (!deletion->isEmpty()
			&& !ParseDeletedMessageContext(*deletion))
		|| (!engagement->isEmpty()
			&& (deletion->isEmpty()
				|| !ParseDeletedMessageEngagement(*engagement)))) {
		return ParseSupportFailure(ParseError::Corrupt);
	}
	return {
		.value = MessageSnapshotSupport{
			.media = *media,
			.replyMarkup = *replyMarkup,
			.deletion = *deletion,
			.engagement = *engagement,
		},
		.error = ParseError::None,
	};
}

namespace {

std::optional<MessageSnapshot> MakeSnapshot(
		not_null<const HistoryItem*> item,
		const TextWithEntities &text,
		const Data::Media *mediaData,
		bool invertMedia,
		TimeId versionDate,
		QByteArray deletion = {},
		QByteArray engagement = {}) {
	if (!item->isRegular()
		|| item->isService()
		|| item->isSponsored()
		|| item->isLegacyMessage()
		|| !item->computeUnavailableReason().isEmpty()
		|| item->Has<HistoryMessageRichPageSource>()
		|| item->Has<HistoryMessageFactcheck>()
		|| item->Has<HistoryMessageSuggestion>()
		|| item->Has<HistoryMessageFromRank>()) {
		return std::nullopt;
	}
	const auto media = SerializeMessageMedia(mediaData);
	if (!media) {
		return std::nullopt;
	}
	const auto visibleMedia = SerializeMessageMediaVisible({
		.type = media->type,
		.invertMedia = (media->type != MessageMediaType::None)
			&& !text.empty()
			&& invertMedia,
		.record = media->visible,
	});
	const auto markup = item->Get<HistoryMessageReplyMarkup>();
	const auto serializedMarkup = SerializeReplyMarkup(
		markup ? &markup->data : nullptr);
	if (!visibleMedia || !serializedMarkup) {
		return std::nullopt;
	}
	const auto support = SerializeMessageSnapshotSupport({
		.media = media->support,
		.replyMarkup = serializedMarkup->support,
		.deletion = std::move(deletion),
		.engagement = std::move(engagement),
	});
	if (!support) {
		return std::nullopt;
	}
	return MessageSnapshot{
		.versionDate = versionDate,
		.service = false,
		.text = text,
		.media = *visibleMedia,
		.replyMarkup = serializedMarkup->visible,
		.support = *support,
	};
}

} // namespace

std::optional<MessageSnapshot> MakeMessageSnapshot(
		not_null<const HistoryItem*> item) {
	if (item->isEditingMedia()) {
		return std::nullopt;
	}
	const auto edited = item->Get<HistoryMessageEdited>();
	return MakeSnapshot(
		item,
		item->originalText(),
		item->media(),
		item->invertMedia(),
		(edited && edited->date) ? edited->date : item->date());
}

std::optional<MessageSnapshot> MakeSavedMediaSnapshot(
		not_null<const HistoryItem*> item) {
	const auto saved = item->Get<HistoryMessageSavedMediaData>();
	return saved
		? MakeSnapshot(
			item,
			saved->text,
			saved->media.get(),
			saved->invertMedia,
			(saved->hadEditedComponent && saved->editDate)
				? saved->editDate
				: item->date())
		: std::nullopt;
}

std::optional<MessageSnapshot> MakeDeletedMessageSnapshot(
		not_null<const HistoryItem*> item) {
	if (item->isEditingMedia()) {
		return std::nullopt;
	}
	const auto deletion = SerializeDeletedMessageContext(item);
	const auto engagement = SerializeDeletedMessageEngagement(item);
	if (!deletion || !engagement) {
		return std::nullopt;
	}
	const auto edited = item->Get<HistoryMessageEdited>();
	return MakeSnapshot(
		item,
		item->originalText(),
		item->media(),
		item->invertMedia(),
		(edited && edited->date) ? edited->date : item->date(),
		*deletion,
		*engagement);
}

#ifdef _DEBUG
void ValidateMessageSnapshotFormat() {
	static const auto checked = [] {
		CheckMessageSnapshotFormat();
		return true;
	}();
	Q_UNUSED(checked);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
