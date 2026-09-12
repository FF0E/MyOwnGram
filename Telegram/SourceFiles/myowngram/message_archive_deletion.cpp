// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_deletion.h"

#include "data/data_peer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "myowngram/message_archive_peer.h"

#include <QtCore/QDataStream>

#include <limits>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kDeletedMessageContextVersion = uint16(1);
constexpr auto kFlagPost = quint8(1 << 0);
constexpr auto kFlagAuthorHidden = quint8(1 << 1);
constexpr auto kFlagNoForwards = quint8(1 << 2);
constexpr auto kFlagHideEdited = quint8(1 << 3);
constexpr auto kKnownFlags = kFlagPost
	| kFlagAuthorHidden
	| kFlagNoForwards
	| kFlagHideEdited;

ParsedDeletedMessageContext ParseFailure(ParseError error) {
	return { .error = error };
}

bool GoodContext(const DeletedMessageContext &context) {
	return IsValidArchivePeerId(context.from)
		&& context.date > 0
		&& context.editDate >= 0
		&& (!context.authorHidden || context.post);
}

quint8 ContextFlags(const DeletedMessageContext &context) {
	return (context.post ? kFlagPost : 0)
		| (context.authorHidden ? kFlagAuthorHidden : 0)
		| (context.noForwards ? kFlagNoForwards : 0)
		| (context.hideEdited ? kFlagHideEdited : 0);
}

bool HasUnsupportedContext(not_null<const HistoryItem*> item) {
	return item->out()
		|| item->Get<HistoryMessageReply>()
		|| item->Get<HistoryMessageForwarded>()
		|| item->Get<HistoryMessageMediaForInstantView>()
		|| item->Get<HistoryMessageSigned>()
		|| item->Get<HistoryMessageGuestChat>()
		|| item->Get<HistoryMessageFromRank>()
		|| item->Get<HistoryMessageSaved>()
		|| item->Get<HistoryServiceSelfDestruct>()
		|| item->viaBot()
		|| item->effectId()
		|| item->boostsApplied()
		|| item->starsPaid()
		|| item->hasUnpaidContent()
		|| item->hasPossibleRestrictions()
		|| item->textAppearing();
}

#ifdef _DEBUG
void CheckDeletedMessageContextFormat() {
	const auto context = DeletedMessageContext{
		.from = peerFromUser(UserId(123)),
		.date = 456,
		.editDate = 456,
		.groupedId = 789,
		.post = true,
		.authorHidden = true,
		.noForwards = true,
		.hideEdited = true,
	};
	const auto serialized = SerializeDeletedMessageContext(context);
	Assert(serialized.has_value());
	const auto parsed = ParseDeletedMessageContext(*serialized);
	Assert(parsed && parsed.value == context);

	auto invalid = context;
	invalid.from = PeerId();
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid.from = peerFromChat(ChatId());
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid.from = PeerId(FakeChatId(123));
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid.from = PeerId(PeerIdHelper(
		peerFromUser(UserId(123)).value | (uint64(1) << 56)));
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid = context;
	invalid.post = false;
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid = context;
	invalid.editDate = -1;
	Assert(!SerializeDeletedMessageContext(invalid));
	invalid.editDate = 0;
	const auto unedited = SerializeDeletedMessageContext(invalid);
	Assert(unedited && ParseDeletedMessageContext(*unedited).value == invalid);

	const auto record = ParseRecord(
		*serialized,
		RecordType::DeletedMessageContext,
		kDeletedMessageContextVersion);
	Assert(record);
	auto payload = record.payload;
	payload[payload.size() - 1] = char(255);
	const auto unknownFlags = SerializeRecord(
		RecordType::DeletedMessageContext,
		kDeletedMessageContextVersion,
		payload);
	Assert(unknownFlags.has_value());
	Assert(!ParseDeletedMessageContext(*unknownFlags));

	auto unknownPeerPayload = QByteArray();
	auto unknownPeerStream = QDataStream(
		&unknownPeerPayload,
		QIODevice::WriteOnly);
	unknownPeerStream.setVersion(QDataStream::Qt_5_1);
	unknownPeerStream.setByteOrder(QDataStream::BigEndian);
	unknownPeerStream
		<< quint64((uint64(UserId::kReservedBit | 3) << 48) | 123)
		<< qint64(context.date)
		<< qint64(context.editDate)
		<< quint64(context.groupedId)
		<< ContextFlags(context);
	Assert(unknownPeerStream.status() == QDataStream::Ok);
	const auto unknownPeer = SerializeRecord(
		RecordType::DeletedMessageContext,
		kDeletedMessageContextVersion,
		unknownPeerPayload);
	Assert(unknownPeer.has_value());
	Assert(!ParseDeletedMessageContext(*unknownPeer));

	auto truncated = *serialized;
	truncated.chop(1);
	Assert(!ParseDeletedMessageContext(truncated));
}
#endif // _DEBUG

} // namespace

std::optional<QByteArray> SerializeDeletedMessageContext(
		const DeletedMessageContext &context) {
	const auto from = SerializeArchivePeerId(context.from);
	if (!GoodContext(context) || !from) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< quint64(*from)
		<< qint64(context.date)
		<< qint64(context.editDate)
		<< quint64(context.groupedId)
		<< ContextFlags(context);
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::DeletedMessageContext,
		kDeletedMessageContextVersion,
		payload);
}

std::optional<QByteArray> SerializeDeletedMessageContext(
		not_null<const HistoryItem*> item) {
	if (HasUnsupportedContext(item)) {
		return std::nullopt;
	}
	const auto edited = item->Get<HistoryMessageEdited>();
	return SerializeDeletedMessageContext({
		.from = item->from()->id,
		.date = item->date(),
		.editDate = edited ? edited->date : 0,
		.groupedId = item->groupId().raw(),
		.post = item->isPost(),
		.authorHidden = item->isPostHidingAuthor(),
		.noForwards = item->forbidsForward(),
		.hideEdited = item->hideEditedBadge(),
	});
}

ParsedDeletedMessageContext ParseDeletedMessageContext(
		const QByteArray &serialized) {
	const auto record = ParseRecord(
		serialized,
		RecordType::DeletedMessageContext,
		kDeletedMessageContextVersion);
	if (!record) {
		return ParseFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto from = quint64();
	auto date = qint64();
	auto editDate = qint64();
	auto groupedId = quint64();
	auto flags = quint8();
	stream >> from >> date >> editDate >> groupedId >> flags;
	const auto fromId = DeserializeArchivePeerId(from);
	if (stream.status() != QDataStream::Ok
		|| (flags & ~kKnownFlags)
		|| date <= 0
		|| date > std::numeric_limits<TimeId>::max()
		|| editDate < 0
		|| editDate > std::numeric_limits<TimeId>::max()
		|| !fromId) {
		return ParseFailure(ParseError::Corrupt);
	}
	auto result = DeletedMessageContext{
		.from = *fromId,
		.date = TimeId(date),
		.editDate = TimeId(editDate),
		.groupedId = groupedId,
		.post = (flags & kFlagPost) != 0,
		.authorHidden = (flags & kFlagAuthorHidden) != 0,
		.noForwards = (flags & kFlagNoForwards) != 0,
		.hideEdited = (flags & kFlagHideEdited) != 0,
	};
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| !GoodContext(result)) {
		return ParseFailure(ParseError::Corrupt);
	}
	return {
		.value = std::move(result),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
void ValidateDeletedMessageContextFormat() {
	static const auto checked = [] {
		CheckDeletedMessageContextFormat();
		return true;
	}();
	Q_UNUSED(checked);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
