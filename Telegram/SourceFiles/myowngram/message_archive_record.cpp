// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_record.h"

#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

#include <algorithm>
#include <limits>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kKeyPartBits = 56;
constexpr auto kKeyPartMask = (uint64(1) << kKeyPartBits) - 1;
constexpr auto kKeyMagic = uint64(0x4D) << kKeyPartBits;
constexpr auto kRecordMagic = quint32(0x4D4F4741);
constexpr auto kRecordHeaderSize = int(
	sizeof(quint32) + sizeof(quint16) + sizeof(quint16) + sizeof(quint32));

constexpr Storage::Cache::Key MakeKey(
		RecordType type,
		uint64 peer,
		uint64 position) {
	return {
		kKeyMagic | peer,
		(uint64(type) << kKeyPartBits) | position,
	};
}

constexpr auto kMessagePositionSegmentBits = 8;
constexpr auto kMessagePositionPrefixBits
	= kKeyPartBits - kMessagePositionSegmentBits;
static_assert(
	kKeyPartBits / kMessagePositionSegmentBits == kMessagePositionLevels);

auto MakeMessagePositionPath(
		uint64 peer,
		uint64 position)
-> std::array<MessagePositionEntry, kMessagePositionLevels> {
	auto result = std::array<
		MessagePositionEntry,
		kMessagePositionLevels>();
	for (auto level = 0; level != kMessagePositionLevels; ++level) {
		const auto prefixBits = level * kMessagePositionSegmentBits;
		const auto prefix = prefixBits
			? (position >> (kKeyPartBits - prefixBits))
			: 0;
		const auto bitShift = kKeyPartBits
			- ((level + 1) * kMessagePositionSegmentBits);
		result[level] = {
			MakeKey(
				RecordType::MessagePositionIndex,
				peer,
				(uint64(level) << kMessagePositionPrefixBits) | prefix),
			uint8(position >> bitShift),
		};
	}
	return result;
}

static_assert(MakeKey(RecordType::MessageTimeline, 1, 2).high
	== (kKeyMagic | 1));
static_assert(MakeKey(RecordType::MessageTimeline, 1, 2).low
	== ((uint64(RecordType::MessageTimeline) << kKeyPartBits) | 2));
static_assert(MakeKey(RecordType::LocalDeleteJobs, 0, 0).high == kKeyMagic);
static_assert(MakeKey(RecordType::LocalDeleteJobs, 0, 0).low
	== (uint64(RecordType::LocalDeleteJobs) << kKeyPartBits));
static_assert(MakeKey(RecordType::MessageArchiveSequence, 0, 0).high
	== kKeyMagic);
static_assert(MakeKey(RecordType::MessageArchiveSequence, 0, 0).low
	== (uint64(RecordType::MessageArchiveSequence) << kKeyPartBits));

ParsedRecord ParseFailure(ParseError error) {
	return { .error = error };
}

} // namespace

Storage::Cache::Key LocalDeleteJobsKey() {
	return MakeKey(RecordType::LocalDeleteJobs, 0, 0);
}

Storage::Cache::Key MessageArchiveSequenceKey() {
	return MakeKey(RecordType::MessageArchiveSequence, 0, 0);
}

Storage::Cache::Key MessageTimelineKey(FullMsgId id) {
	Expects(id.peer);
	Expects(IsServerMsgId(id.msg));

	const auto peer = SerializePeerId(id.peer);
	const auto position = uint64(id.msg.bare);
	Assert((peer & ~kKeyPartMask) == 0);
	Assert((position & ~kKeyPartMask) == 0);
	return MakeKey(RecordType::MessageTimeline, peer, position);
}

auto MessagePositionPath(FullMsgId id)
-> std::array<MessagePositionEntry, kMessagePositionLevels> {
	Expects(id.peer);
	Expects(IsServerMsgId(id.msg));

	const auto peer = SerializePeerId(id.peer);
	const auto position = uint64(id.msg.bare);
	Assert((peer & ~kKeyPartMask) == 0);
	Assert((position & ~kKeyPartMask) == 0);
	return MakeMessagePositionPath(peer, position);
}

std::optional<QByteArray> SerializeRecord(
		RecordType type,
		uint16 version,
		const QByteArray &payload) {
	Expects(uint8(type) != 0);
	Expects(version != 0);

	if (payload.size()
		> (Storage::kMessageArchiveMaxRecordSize - kRecordHeaderSize)) {
		return std::nullopt;
	}
	auto result = QByteArray();
	result.reserve(kRecordHeaderSize + payload.size());
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< kRecordMagic
		<< quint16(type)
		<< quint16(version)
		<< quint32(payload.size());
	const auto written = payload.isEmpty()
		? 0
		: stream.writeRawData(payload.constData(), payload.size());
	if (stream.status() != QDataStream::Ok || written != payload.size()) {
		return std::nullopt;
	}
	return result;
}

ParsedRecordHeader ParseRecordHeader(
		const QByteArray &serialized,
		RecordType expectedType,
		uint16 expectedVersion) {
	Expects(uint8(expectedType) != 0);
	Expects(expectedVersion != 0);

	if (serialized.size() < kRecordHeaderSize
		|| serialized.size() > Storage::kMessageArchiveMaxRecordSize) {
		return { .error = ParseError::Corrupt };
	}
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto magic = quint32();
	auto type = quint16();
	auto version = quint16();
	auto payloadSize = quint32();
	stream >> magic >> type >> version >> payloadSize;
	if (stream.status() != QDataStream::Ok || magic != kRecordMagic) {
		return { .error = ParseError::Corrupt };
	}
	const auto expectedPayloadSize = serialized.size() - kRecordHeaderSize;
	if (payloadSize != quint32(expectedPayloadSize)) {
		return { .error = ParseError::Corrupt };
	} else if (type != quint16(expectedType)) {
		return { .error = ParseError::WrongType };
	} else if (!version) {
		return { .error = ParseError::Corrupt };
	} else if (version != expectedVersion) {
		return { .error = ParseError::UnsupportedVersion };
	}
	return {
		.version = version,
		.error = ParseError::None,
		.payloadOffset = kRecordHeaderSize,
		.payloadSize = expectedPayloadSize,
	};
}

ParsedRecord ParseRecord(
		const QByteArray &serialized,
		RecordType expectedType,
		uint16 expectedVersion) {
	const auto header = ParseRecordHeader(
		serialized,
		expectedType,
		expectedVersion);
	if (!header) {
		return ParseFailure(header.error);
	}
	auto payload = QByteArray(header.payloadSize, Qt::Uninitialized);
	if (!payload.isEmpty()) {
		std::copy_n(
			serialized.constData() + header.payloadOffset,
			payload.size(),
			payload.data());
	}
	return {
		.version = header.version,
		.error = ParseError::None,
		.payload = std::move(payload),
	};
}

namespace Binary {

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

} // namespace Binary
} // namespace MyOwnGram::MessageArchiveStorage
