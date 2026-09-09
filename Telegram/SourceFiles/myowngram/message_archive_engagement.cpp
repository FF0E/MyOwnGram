// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_engagement.h"

#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "myowngram/message_archive_peer.h"

#include <QtCore/QDataStream>

#include <limits>
#include <map>
#include <set>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kDeletedMessageEngagementVersion = uint16(1);
constexpr auto kFlagViews = quint8(1 << 0);
constexpr auto kFlagCanViewReactions = quint8(1 << 1);
constexpr auto kKnownFlags = kFlagViews | kFlagCanViewReactions;
constexpr auto kFlagMy = quint8(1 << 0);
constexpr auto kFlagTop = quint8(1 << 1);

enum class ReactionWireType : quint8 {
	Emoji = 1,
	Custom = 2,
	Paid = 3,
};

ParsedDeletedMessageEngagement ParseFailure(ParseError error) {
	return { .error = error };
}

bool GoodCount(int count) {
	return count >= -1;
}

bool GoodSize(size_t size) {
	return size <= std::numeric_limits<quint32>::max();
}

bool GoodViews(const DeletedMessageViews &views) {
	if (!GoodCount(views.views)
		|| !GoodCount(views.replies)
		|| !GoodCount(views.forwards)
		|| !GoodSize(views.recentRepliers.size())
		|| (views.commentsMegagroupId
			&& views.commentsMegagroupId.bare > PeerId::kChatTypeMask)
		|| (views.commentsRootId
			&& (!views.commentsMegagroupId
				|| !IsServerMsgId(views.commentsRootId)))) {
		return false;
	}
	return ranges::all_of(views.recentRepliers, IsValidArchivePeerId);
}

bool GoodReactionId(const Data::ReactionId &id) {
	return id.paid() || id.custom() || !id.emoji().isEmpty();
}

bool GoodEngagement(const DeletedMessageEngagement &engagement) {
	if ((engagement.views && !GoodViews(*engagement.views))
		|| !GoodSize(engagement.reactions.size())
		|| !GoodSize(engagement.recentReactions.size())
		|| !GoodSize(engagement.topPaidReactions.size())
		|| (engagement.canViewReactions
			&& engagement.reactions.empty())) {
		return false;
	}
	auto reactions = std::map<Data::ReactionId, int>();
	for (const auto &reaction : engagement.reactions) {
		if (!GoodReactionId(reaction.id)
			|| reaction.count <= 0
			|| !reactions.emplace(reaction.id, reaction.count).second) {
			return false;
		}
	}
	auto recent = std::set<Data::ReactionId>();
	for (const auto &group : engagement.recentReactions) {
		const auto reaction = reactions.find(group.id);
		if (reaction == end(reactions)
			|| group.list.empty()
			|| !GoodSize(group.list.size())
			|| group.list.size() > size_t(reaction->second)
			|| !recent.emplace(group.id).second
			|| !ranges::all_of(group.list, [](const auto &entry) {
				return IsValidArchivePeerId(entry.peer);
			})) {
			return false;
		}
	}
	for (const auto &entry : engagement.topPaidReactions) {
		if (entry.count <= 0
			|| (entry.peer && !IsValidArchivePeerId(entry.peer))) {
			return false;
		}
	}
	return engagement.topPaidReactions.empty()
		|| reactions.contains(Data::ReactionId::Paid());
}

bool EmptyEngagement(const DeletedMessageEngagement &engagement) {
	return !engagement.views
		&& engagement.reactions.empty()
		&& engagement.recentReactions.empty()
		&& engagement.topPaidReactions.empty()
		&& !engagement.canViewReactions;
}

bool GoodWireCount(
		QDataStream &stream,
		quint32 count,
		int minimumBytes) {
	const auto available = stream.device()->bytesAvailable();
	return stream.status() == QDataStream::Ok
		&& available >= 0
		&& uint64(count) <= uint64(available) / minimumBytes;
}

bool WritePeerId(QDataStream &stream, PeerId id) {
	const auto serialized = SerializeArchivePeerId(id);
	if (!serialized) {
		return false;
	}
	stream << quint64(*serialized);
	return stream.status() == QDataStream::Ok;
}

std::optional<PeerId> ReadPeerId(QDataStream &stream) {
	auto serialized = quint64();
	stream >> serialized;
	return (stream.status() == QDataStream::Ok)
		? DeserializeArchivePeerId(serialized)
		: std::nullopt;
}

bool WriteReactionId(QDataStream &stream, const Data::ReactionId &id) {
	if (!GoodReactionId(id)) {
		return false;
	} else if (id.paid()) {
		stream << quint8(ReactionWireType::Paid);
	} else if (const auto custom = id.custom()) {
		stream
			<< quint8(ReactionWireType::Custom)
			<< quint64(custom);
	} else {
		stream << quint8(ReactionWireType::Emoji);
		return Binary::WriteString(stream, id.emoji());
	}
	return stream.status() == QDataStream::Ok;
}

std::optional<Data::ReactionId> ReadReactionId(QDataStream &stream) {
	auto type = quint8();
	stream >> type;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	switch (ReactionWireType(type)) {
	case ReactionWireType::Emoji: {
		const auto emoji = Binary::ReadString(stream);
		if (!emoji) {
			return std::nullopt;
		}
		const auto result = Data::ReactionId{ *emoji };
		return GoodReactionId(result) && !result.paid()
			? std::optional<Data::ReactionId>(result)
			: std::nullopt;
	}
	case ReactionWireType::Custom: {
		auto custom = quint64();
		stream >> custom;
		return (stream.status() == QDataStream::Ok && custom)
			? std::optional<Data::ReactionId>(Data::ReactionId{ custom })
			: std::nullopt;
	}
	case ReactionWireType::Paid:
		return Data::ReactionId::Paid();
	}
	return std::nullopt;
}

quint8 EngagementFlags(const DeletedMessageEngagement &engagement) {
	return (engagement.views ? kFlagViews : 0)
		| (engagement.canViewReactions ? kFlagCanViewReactions : 0);
}

bool WriteViews(QDataStream &stream, const DeletedMessageViews &views) {
	stream
		<< qint32(views.views)
		<< qint32(views.replies)
		<< qint32(views.forwards)
		<< quint64(views.commentsMegagroupId.bare)
		<< qint64(views.commentsRootId.bare)
		<< quint32(views.recentRepliers.size());
	for (const auto peer : views.recentRepliers) {
		if (!WritePeerId(stream, peer)) {
			return false;
		}
	}
	return stream.status() == QDataStream::Ok;
}

bool WriteReactions(
		QDataStream &stream,
		const DeletedMessageEngagement &engagement) {
	stream << quint32(engagement.reactions.size());
	for (const auto &reaction : engagement.reactions) {
		if (!WriteReactionId(stream, reaction.id)) {
			return false;
		}
		stream
			<< qint32(reaction.count)
			<< quint8(reaction.my ? kFlagMy : 0);
	}
	stream << quint32(engagement.recentReactions.size());
	for (const auto &group : engagement.recentReactions) {
		if (!WriteReactionId(stream, group.id)) {
			return false;
		}
		stream << quint32(group.list.size());
		for (const auto &entry : group.list) {
			if (!WritePeerId(stream, entry.peer)) {
				return false;
			}
			stream << quint8(entry.my ? kFlagMy : 0);
		}
	}
	stream << quint32(engagement.topPaidReactions.size());
	for (const auto &entry : engagement.topPaidReactions) {
		if (entry.peer) {
			if (!WritePeerId(stream, entry.peer)) {
				return false;
			}
		} else {
			stream << quint64(0);
		}
		stream
			<< qint32(entry.count)
			<< quint8((entry.top ? kFlagTop : 0)
				| (entry.my ? kFlagMy : 0));
	}
	return stream.status() == QDataStream::Ok;
}

#ifdef _DEBUG
void CheckDeletedMessageEngagementFormat() {
	const auto emoji = Data::ReactionId{ u"👍"_q };
	const auto custom = Data::ReactionId{ DocumentId(123) };
	const auto paid = Data::ReactionId::Paid();
	const auto engagement = DeletedMessageEngagement{
		.views = DeletedMessageViews{
			.views = 100,
			.replies = 4,
			.forwards = 3,
			.recentRepliers = {
				peerFromUser(UserId(1)),
				peerFromChat(ChatId(2)),
			},
			.commentsMegagroupId = ChannelId(3),
			.commentsRootId = MsgId(4),
		},
		.reactions = {
			{ .id = emoji, .count = 2, .my = true },
			{ .id = custom, .count = 1 },
			{ .id = paid, .count = 7, .my = true },
		},
		.recentReactions = {
			{
				.id = emoji,
				.list = {
					{ .peer = peerFromUser(UserId(1)), .my = true },
					{ .peer = peerFromChannel(ChannelId(3)) },
				},
			},
		},
		.topPaidReactions = {
			{
				.peer = peerFromUser(UserId(1)),
				.count = 4,
				.top = true,
				.my = true,
			},
			{ .count = 3, .top = true },
		},
		.canViewReactions = true,
	};
	const auto serialized = SerializeDeletedMessageEngagement(engagement);
	Assert(serialized.has_value());
	const auto parsed = ParseDeletedMessageEngagement(*serialized);
	Assert(parsed && parsed.value == engagement);
	const auto empty = SerializeDeletedMessageEngagement({});
	Assert(empty && empty->isEmpty());
	Assert(ParseDeletedMessageEngagement(QByteArray()));

	auto invalid = engagement;
	invalid.views->commentsMegagroupId = ChannelId();
	Assert(!SerializeDeletedMessageEngagement(invalid));
	invalid = engagement;
	invalid.views->recentRepliers[0] = PeerId(FakeChatId(1));
	Assert(!SerializeDeletedMessageEngagement(invalid));
	invalid = engagement;
	invalid.reactions[0].count = 0;
	Assert(!SerializeDeletedMessageEngagement(invalid));
	invalid = engagement;
	invalid.reactions.push_back(invalid.reactions[0]);
	Assert(!SerializeDeletedMessageEngagement(invalid));
	invalid = engagement;
	invalid.recentReactions[0].id = Data::ReactionId{ u"👎"_q };
	Assert(!SerializeDeletedMessageEngagement(invalid));
	invalid = engagement;
	invalid.reactions.pop_back();
	Assert(!SerializeDeletedMessageEngagement(invalid));

	const auto record = ParseRecord(
		*serialized,
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion);
	Assert(record);
	auto payload = record.payload;
	payload[0] = char(255);
	const auto unknownFlags = SerializeRecord(
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion,
		payload);
	Assert(unknownFlags.has_value());
	Assert(!ParseDeletedMessageEngagement(*unknownFlags));

	const auto reactionOnly = DeletedMessageEngagement{
		.reactions = { { .id = paid, .count = 1 } },
	};
	const auto serializedReactionOnly = SerializeDeletedMessageEngagement(
		reactionOnly);
	Assert(serializedReactionOnly.has_value());
	const auto reactionRecord = ParseRecord(
		*serializedReactionOnly,
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion);
	Assert(reactionRecord);
	payload = reactionRecord.payload;
	payload[sizeof(quint8) + sizeof(quint32)] = char(255);
	const auto unknownReaction = SerializeRecord(
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion,
		payload);
	Assert(unknownReaction.has_value());
	Assert(!ParseDeletedMessageEngagement(*unknownReaction));

	auto noncanonicalPayload = QByteArray();
	auto noncanonicalStream = QDataStream(
		&noncanonicalPayload,
		QIODevice::WriteOnly);
	noncanonicalStream.setVersion(QDataStream::Qt_5_1);
	noncanonicalStream.setByteOrder(QDataStream::BigEndian);
	noncanonicalStream
		<< quint8(0)
		<< quint32(1)
		<< quint8(ReactionWireType::Emoji);
	Assert(Binary::WriteString(noncanonicalStream, u"*"_q));
	noncanonicalStream
		<< qint32(1)
		<< quint8(0)
		<< quint32(0)
		<< quint32(0);
	Assert(noncanonicalStream.status() == QDataStream::Ok);
	const auto noncanonical = SerializeRecord(
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion,
		noncanonicalPayload);
	Assert(noncanonical.has_value());
	Assert(!ParseDeletedMessageEngagement(*noncanonical));

	auto truncated = *serialized;
	truncated.chop(1);
	Assert(!ParseDeletedMessageEngagement(truncated));
	const auto future = SerializeRecord(
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion + 1,
		QByteArray());
	Assert(future.has_value());
	Assert(ParseDeletedMessageEngagement(*future).error
		== ParseError::UnsupportedVersion);
}
#endif // _DEBUG

} // namespace

std::optional<QByteArray> SerializeDeletedMessageEngagement(
		const DeletedMessageEngagement &engagement) {
	if (!GoodEngagement(engagement)) {
		return std::nullopt;
	} else if (EmptyEngagement(engagement)) {
		return QByteArray();
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream << EngagementFlags(engagement);
	if ((engagement.views && !WriteViews(stream, *engagement.views))
		|| !WriteReactions(stream, engagement)
		|| stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion,
		payload);
}

std::optional<QByteArray> SerializeDeletedMessageEngagement(
		not_null<const HistoryItem*> item) {
	if (item->reactionsAreTags()) {
		return std::nullopt;
	}
	auto result = DeletedMessageEngagement();
	if (const auto views = item->Get<HistoryMessageViews>()) {
		result.views = DeletedMessageViews{
			.views = views->views.count,
			.replies = views->replies.count,
			.forwards = views->forwardsCount,
			.recentRepliers = views->recentRepliers,
			.commentsMegagroupId = views->commentsMegagroupId,
			.commentsRootId = views->commentsRootId,
		};
	}
	for (const auto &reaction : item->reactionsWithLocal()) {
		result.reactions.push_back({
			.id = reaction.id,
			.count = reaction.count,
			.my = reaction.my,
		});
	}
	for (const auto &[id, list] : item->recentReactions()) {
		auto &group = result.recentReactions.emplace_back();
		group.id = id;
		for (const auto &entry : list) {
			group.list.push_back({
				.peer = entry.peer->id,
				.my = entry.my,
			});
		}
	}
	for (const auto &entry : item->topPaidReactionsWithLocal()) {
		result.topPaidReactions.push_back({
			.peer = entry.peer ? entry.peer->id : PeerId(),
			.count = int(entry.count),
			.top = (entry.top != 0),
			.my = (entry.my != 0),
		});
	}
	result.canViewReactions = item->canViewReactions();
	return SerializeDeletedMessageEngagement(result);
}

ParsedDeletedMessageEngagement ParseDeletedMessageEngagement(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {
			.value = DeletedMessageEngagement(),
			.error = ParseError::None,
		};
	}
	const auto record = ParseRecord(
		serialized,
		RecordType::DeletedMessageEngagement,
		kDeletedMessageEngagementVersion);
	if (!record) {
		return ParseFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto flags = quint8();
	stream >> flags;
	if (stream.status() != QDataStream::Ok || (flags & ~kKnownFlags)) {
		return ParseFailure(ParseError::Corrupt);
	}
	auto result = DeletedMessageEngagement{
		.canViewReactions = (flags & kFlagCanViewReactions) != 0,
	};
	if (flags & kFlagViews) {
		auto views = qint32();
		auto replies = qint32();
		auto forwards = qint32();
		auto commentsMegagroupId = quint64();
		auto commentsRootId = qint64();
		auto recentCount = quint32();
		stream
			>> views
			>> replies
			>> forwards
			>> commentsMegagroupId
			>> commentsRootId
			>> recentCount;
		if (!GoodWireCount(stream, recentCount, sizeof(quint64))) {
			return ParseFailure(ParseError::Corrupt);
		}
		auto parsedViews = DeletedMessageViews{
			.views = views,
			.replies = replies,
			.forwards = forwards,
			.commentsMegagroupId = ChannelId(commentsMegagroupId),
			.commentsRootId = MsgId(commentsRootId),
		};
		parsedViews.recentRepliers.reserve(recentCount);
		for (auto i = quint32(); i != recentCount; ++i) {
			const auto peer = ReadPeerId(stream);
			if (!peer) {
				return ParseFailure(ParseError::Corrupt);
			}
			parsedViews.recentRepliers.push_back(*peer);
		}
		result.views = std::move(parsedViews);
	}
	auto reactionCount = quint32();
	stream >> reactionCount;
	if (!GoodWireCount(stream, reactionCount, 6)) {
		return ParseFailure(ParseError::Corrupt);
	}
	result.reactions.reserve(reactionCount);
	for (auto i = quint32(); i != reactionCount; ++i) {
		const auto id = ReadReactionId(stream);
		auto count = qint32();
		auto reactionFlags = quint8();
		stream >> count >> reactionFlags;
		if (!id
			|| stream.status() != QDataStream::Ok
			|| count <= 0
			|| (reactionFlags & ~kFlagMy)) {
			return ParseFailure(ParseError::Corrupt);
		}
		result.reactions.push_back({
			.id = *id,
			.count = count,
			.my = (reactionFlags & kFlagMy) != 0,
		});
	}
	auto recentCount = quint32();
	stream >> recentCount;
	if (!GoodWireCount(stream, recentCount, 5)) {
		return ParseFailure(ParseError::Corrupt);
	}
	result.recentReactions.reserve(recentCount);
	for (auto i = quint32(); i != recentCount; ++i) {
		const auto id = ReadReactionId(stream);
		auto participantCount = quint32();
		stream >> participantCount;
		if (!id
			|| !participantCount
			|| !GoodWireCount(stream, participantCount, 9)) {
			return ParseFailure(ParseError::Corrupt);
		}
		auto &group = result.recentReactions.emplace_back();
		group.id = *id;
		group.list.reserve(participantCount);
		for (auto j = quint32(); j != participantCount; ++j) {
			const auto peer = ReadPeerId(stream);
			auto participantFlags = quint8();
			stream >> participantFlags;
			if (!peer
				|| stream.status() != QDataStream::Ok
				|| (participantFlags & ~kFlagMy)) {
				return ParseFailure(ParseError::Corrupt);
			}
			group.list.push_back({
				.peer = *peer,
				.my = (participantFlags & kFlagMy) != 0,
			});
		}
	}
	auto topPaidCount = quint32();
	stream >> topPaidCount;
	if (!GoodWireCount(stream, topPaidCount, 13)) {
		return ParseFailure(ParseError::Corrupt);
	}
	result.topPaidReactions.reserve(topPaidCount);
	for (auto i = quint32(); i != topPaidCount; ++i) {
		auto serializedPeer = quint64();
		auto count = qint32();
		auto topFlags = quint8();
		stream >> serializedPeer >> count >> topFlags;
		const auto peer = serializedPeer
			? DeserializeArchivePeerId(serializedPeer)
			: std::optional<PeerId>(PeerId());
		if (!peer
			|| stream.status() != QDataStream::Ok
			|| count <= 0
			|| (topFlags & ~(kFlagTop | kFlagMy))) {
			return ParseFailure(ParseError::Corrupt);
		}
		result.topPaidReactions.push_back({
			.peer = *peer,
			.count = count,
			.top = (topFlags & kFlagTop) != 0,
			.my = (topFlags & kFlagMy) != 0,
		});
	}
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| !GoodEngagement(result)) {
		return ParseFailure(ParseError::Corrupt);
	}
	return {
		.value = std::move(result),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
void ValidateDeletedMessageEngagementFormat() {
	static const auto checked = [] {
		CheckDeletedMessageEngagementFormat();
		return true;
	}();
	Q_UNUSED(checked);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
