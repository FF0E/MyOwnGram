// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_timeline.h"

#include "lang/lang_keys.h"

#include <QtCore/QDataStream>

#include <algorithm>
#include <array>
#include <limits>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kMessageTimelineVersion = uint16(1);
constexpr auto kKnownTimelineFlags = uint8(
	uint8(MessageTimelineFlag::Deleted)
	| uint8(MessageTimelineFlag::Expired));
constexpr auto kMinimumEntitySize = int(
	sizeof(quint8) + sizeof(qint32) * 2 + sizeof(quint32));
constexpr auto kMinimumSnapshotSize = int(
	sizeof(qint64)
	+ sizeof(quint8)
	+ sizeof(quint32) * 5);

struct EntityTypeMapping {
	quint8 wireId = 0;
	EntityType type = EntityType::Invalid;
};

constexpr auto kEntityTypes = std::array<EntityTypeMapping, 26>{{
	{ 1, EntityType::Url },
	{ 2, EntityType::CustomUrl },
	{ 3, EntityType::Email },
	{ 4, EntityType::Hashtag },
	{ 5, EntityType::Cashtag },
	{ 6, EntityType::Mention },
	{ 7, EntityType::MentionName },
	{ 8, EntityType::CustomEmoji },
	{ 9, EntityType::BotCommand },
	{ 10, EntityType::MediaTimestamp },
	{ 11, EntityType::Colorized },
	{ 12, EntityType::Phone },
	{ 13, EntityType::BankCard },
	{ 14, EntityType::Bold },
	{ 15, EntityType::Semibold },
	{ 16, EntityType::Italic },
	{ 17, EntityType::Underline },
	{ 18, EntityType::StrikeOut },
	{ 19, EntityType::Code },
	{ 20, EntityType::Pre },
	{ 21, EntityType::Blockquote },
	{ 22, EntityType::Spoiler },
	{ 23, EntityType::Subscript },
	{ 24, EntityType::Superscript },
	{ 25, EntityType::Marked },
	{ 26, EntityType::FormattedDate },
}};

std::optional<quint8> SerializeEntityType(EntityType type) {
	const auto i = std::find_if(
		kEntityTypes.begin(),
		kEntityTypes.end(),
		[type](const auto &entry) { return entry.type == type; });
	if (i == kEntityTypes.end()) {
		return std::nullopt;
	}
	return i->wireId;
}

std::optional<EntityType> ParseEntityType(quint8 wireId) {
	const auto i = std::find_if(
		kEntityTypes.begin(),
		kEntityTypes.end(),
		[wireId](const auto &entry) { return entry.wireId == wireId; });
	if (i == kEntityTypes.end()) {
		return std::nullopt;
	}
	return i->type;
}

bool WriteBytes(QDataStream &stream, const QByteArray &value) {
	if (uint64(value.size()) > std::numeric_limits<quint32>::max()) {
		return false;
	}
	stream << quint32(value.size());
	return value.isEmpty()
		|| stream.writeRawData(value.constData(), value.size()) == value.size();
}

std::optional<QByteArray> ReadBytes(QDataStream &stream) {
	auto size = quint32();
	stream >> size;
	if (stream.status() != QDataStream::Ok
		|| size > quint64(stream.device()->bytesAvailable())) {
		return std::nullopt;
	}
	auto result = QByteArray(size, Qt::Uninitialized);
	if (size && stream.readRawData(result.data(), size) != size) {
		return std::nullopt;
	}
	return result;
}

bool WriteString(QDataStream &stream, const QString &value) {
	if (uint64(value.size()) > std::numeric_limits<quint32>::max()) {
		return false;
	}
	stream << quint32(value.size());
	for (const auto character : value) {
		stream << quint16(character.unicode());
	}
	return stream.status() == QDataStream::Ok;
}

std::optional<QString> ReadString(QDataStream &stream) {
	auto size = quint32();
	stream >> size;
	if (stream.status() != QDataStream::Ok
		|| size > quint64(stream.device()->bytesAvailable()) / 2) {
		return std::nullopt;
	}
	const auto count = int(size);
	auto result = QString(count, Qt::Uninitialized);
	for (auto i = 0; i != count; ++i) {
		auto character = quint16();
		stream >> character;
		result[i] = QChar(character);
	}
	return (stream.status() == QDataStream::Ok)
		? std::optional<QString>(result)
		: std::nullopt;
}

bool WriteText(QDataStream &stream, const TextWithEntities &value) {
	if (!WriteString(stream, value.text)
		|| uint64(value.entities.size())
			> std::numeric_limits<quint32>::max()) {
		return false;
	}
	stream << quint32(value.entities.size());
	for (const auto &entity : value.entities) {
		const auto type = SerializeEntityType(entity.type());
		if (!type
			|| entity.offset() < 0
			|| entity.length() < 0
			|| entity.offset() > value.text.size()
			|| entity.length() > value.text.size() - entity.offset()) {
			return false;
		}
		stream
			<< *type
			<< qint32(entity.offset())
			<< qint32(entity.length());
		if (!WriteString(stream, entity.data())) {
			return false;
		}
	}
	return stream.status() == QDataStream::Ok;
}

std::optional<TextWithEntities> ReadText(QDataStream &stream) {
	const auto text = ReadString(stream);
	if (!text) {
		return std::nullopt;
	}
	auto count = quint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| count > quint64(stream.device()->bytesAvailable())
			/ kMinimumEntitySize) {
		return std::nullopt;
	}
	auto result = tr::marked(*text);
	result.entities.reserve(count);
	for (auto i = quint32(); i != count; ++i) {
		auto type = quint8();
		auto offset = qint32();
		auto length = qint32();
		stream >> type >> offset >> length;
		const auto parsedType = ParseEntityType(type);
		const auto data = ReadString(stream);
		if (stream.status() != QDataStream::Ok
			|| !parsedType
			|| !data
			|| offset < 0
			|| length < 0
			|| offset > result.text.size()
			|| length > result.text.size() - offset) {
			return std::nullopt;
		}
		result.entities.push_back(EntityInText(
			*parsedType,
			offset,
			length,
			*data));
	}
	return result;
}

bool WriteSnapshot(QDataStream &stream, const MessageSnapshot &snapshot) {
	stream
		<< qint64(snapshot.versionDate)
		<< quint8(snapshot.service ? 1 : 0);
	return snapshot.versionDate >= 0
		&& WriteText(stream, snapshot.text)
		&& WriteBytes(stream, snapshot.media)
		&& WriteBytes(stream, snapshot.replyMarkup)
		&& WriteBytes(stream, snapshot.support)
		&& stream.status() == QDataStream::Ok;
}

std::optional<MessageSnapshot> ReadSnapshot(QDataStream &stream) {
	auto versionDate = qint64();
	auto service = quint8();
	stream >> versionDate >> service;
	const auto text = ReadText(stream);
	const auto media = ReadBytes(stream);
	const auto replyMarkup = ReadBytes(stream);
	const auto support = ReadBytes(stream);
	if (stream.status() != QDataStream::Ok
		|| versionDate < 0
		|| versionDate > std::numeric_limits<TimeId>::max()
		|| service > 1
		|| !text
		|| !media
		|| !replyMarkup
		|| !support) {
		return std::nullopt;
	}
	return MessageSnapshot{
		.versionDate = TimeId(versionDate),
		.service = (service != 0),
		.text = *text,
		.media = *media,
		.replyMarkup = *replyMarkup,
		.support = *support,
	};
}

ParsedMessageTimeline ParseFailure(ParseError error) {
	return { .error = error };
}

} // namespace

bool SameVisibleContent(
		const MessageSnapshot &a,
		const MessageSnapshot &b) {
	return a.service == b.service
		&& a.text == b.text
		&& a.media == b.media
		&& a.replyMarkup == b.replyMarkup;
}

ObserveResult ObserveVersion(
		MessageTimeline &timeline,
		MessageSnapshot snapshot) {
	if (timeline.versions.empty()
		|| !SameVisibleContent(timeline.versions.back(), snapshot)) {
		timeline.versions.push_back(std::move(snapshot));
		return ObserveResult::Appended;
	}
	auto &current = timeline.versions.back();
	if (current.support == snapshot.support) {
		return ObserveResult::Unchanged;
	}
	current.support = std::move(snapshot.support);
	return ObserveResult::Refreshed;
}

std::optional<QByteArray> SerializeMessageTimeline(
		const MessageTimeline &timeline) {
	// ponytail: Version 1 keeps one message timeline in one 1 MiB record.
	// Once it is full, serialization fails and leaves the prior record intact.
	// This bounds reads and writes but caps revisions for one heavily
	// edited message.
	// Add linked timeline pages before raising the shared record-size limit.
	if (timeline.versions.empty()
		|| (timeline.flags.value() & kKnownTimelineFlags)
			!= timeline.flags.value()
		|| uint64(timeline.versions.size())
			> std::numeric_limits<quint32>::max()) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< quint8(timeline.flags.value())
		<< quint32(timeline.versions.size());
	for (const auto &snapshot : timeline.versions) {
		if (!WriteSnapshot(stream, snapshot)) {
			return std::nullopt;
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::MessageTimeline,
		kMessageTimelineVersion,
		payload);
}

ParsedMessageTimeline ParseMessageTimeline(const QByteArray &serialized) {
	const auto record = ParseRecord(
		serialized,
		RecordType::MessageTimeline,
		kMessageTimelineVersion);
	if (!record) {
		return ParseFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto flags = quint8();
	auto count = quint32();
	stream >> flags >> count;
	if (stream.status() != QDataStream::Ok
		|| (flags & kKnownTimelineFlags) != flags
		|| !count
		|| count > quint64(stream.device()->bytesAvailable())
			/ kMinimumSnapshotSize) {
		return ParseFailure(ParseError::Corrupt);
	}
	auto result = MessageTimeline{
		.flags = MessageTimelineFlags::from_raw(flags),
	};
	result.versions.reserve(count);
	for (auto i = quint32(); i != count; ++i) {
		auto snapshot = ReadSnapshot(stream);
		if (!snapshot) {
			return ParseFailure(ParseError::Corrupt);
		}
		result.versions.push_back(std::move(*snapshot));
	}
	if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
		return ParseFailure(ParseError::Corrupt);
	}
	return {
		.value = std::move(result),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
void ValidateMessageTimelineFormat() {
	static const auto validated = [] {
		for (const auto &entry : kEntityTypes) {
			Assert(ParseEntityType(entry.wireId) == entry.type);
			Assert(SerializeEntityType(entry.type) == entry.wireId);
		}
		Assert(!ParseEntityType(0));
		Assert(!ParseEntityType(255));
		auto first = MessageSnapshot{
			.versionDate = 1,
			.text = tr::marked(u"first"_q),
			.media = "media",
			.replyMarkup = "markup",
			.support = "support-1",
		};
		first.text.entities.push_back(
			EntityInText(EntityType::Bold, 0, 5));
		auto refreshed = first;
		refreshed.versionDate = 2;
		refreshed.support = "support-2";
		auto second = refreshed;
		second.versionDate = 3;
		second.text.text = u"second"_q;
		auto timeline = MessageTimeline();
		Assert(ObserveVersion(timeline, first) == ObserveResult::Appended);
		Assert(ObserveVersion(timeline, refreshed) == ObserveResult::Refreshed);
		Assert(timeline.versions.size() == 1);
		Assert(timeline.versions.front().versionDate == 1);
		Assert(ObserveVersion(timeline, second) == ObserveResult::Appended);
		timeline.flags |= MessageTimelineFlag::Deleted;
		const auto serialized = SerializeMessageTimeline(timeline);
		Assert(serialized.has_value());
		const auto parsed = ParseMessageTimeline(*serialized);
		Assert(parsed && parsed.value == timeline);
		Assert(!ParseMessageTimeline(serialized->chopped(1)));
		const auto future = SerializeRecord(
			RecordType::MessageTimeline,
			kMessageTimelineVersion + 1,
			QByteArray());
		Assert(future.has_value());
		Assert(ParseMessageTimeline(*future).error
			== ParseError::UnsupportedVersion);
		return true;
	}();
	static_cast<void>(validated);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
