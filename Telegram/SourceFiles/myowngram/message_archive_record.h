// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "data/data_peer_id.h"
#include "storage/cache/storage_cache_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <array>
#include <optional>

class QDataStream;
struct FullMsgId;

namespace MyOwnGram::MessageArchiveStorage {

enum class RecordType : uint8 {
	MessageTimeline = 1,
	ReplyMarkupVisible = 2,
	ReplyMarkupSupport = 3,
	PhotoMediaVisible = 4,
	PhotoMediaSupport = 5,
	DocumentMediaVisible = 6,
	DocumentMediaSupport = 7,
	MessageMediaVisible = 8,
	MessageSnapshotSupport = 9,
	DeletedMessageContext = 10,
	DeletedMessageEngagement = 11,
	MessagePositionIndex = 12,
	LocalDeleteJobs = 13,
	MessageArchiveSequence = 14,
};

enum class ParseError : uint8 {
	None,
	Corrupt,
	WrongType,
	UnsupportedVersion,
};

struct ParsedRecordHeader {
	uint16 version = 0;
	ParseError error = ParseError::Corrupt;
	int payloadOffset = 0;
	int payloadSize = 0;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

struct ParsedRecord {
	uint16 version = 0;
	ParseError error = ParseError::Corrupt;
	QByteArray payload;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

constexpr auto kMessagePositionLevels = 7;

struct MessagePositionEntry {
	Storage::Cache::Key key;
	uint8 bit = 0;
};

[[nodiscard]] Storage::Cache::Key LocalDeleteJobsKey();
[[nodiscard]] Storage::Cache::Key MessageArchiveSequenceKey();
[[nodiscard]] Storage::Cache::Key MessageTimelineKey(FullMsgId id);
[[nodiscard]] auto MessagePositionPath(FullMsgId id)
-> std::array<MessagePositionEntry, kMessagePositionLevels>;
[[nodiscard]] std::optional<QByteArray> SerializeRecord(
	RecordType type,
	uint16 version,
	const QByteArray &payload);
[[nodiscard]] ParsedRecordHeader ParseRecordHeader(
	const QByteArray &serialized,
	RecordType expectedType,
	uint16 expectedVersion);
[[nodiscard]] ParsedRecord ParseRecord(
	const QByteArray &serialized,
	RecordType expectedType,
	uint16 expectedVersion);

namespace Binary {

[[nodiscard]] bool WriteBytes(
	QDataStream &stream,
	const QByteArray &value);
[[nodiscard]] std::optional<QByteArray> ReadBytes(QDataStream &stream);
[[nodiscard]] bool WriteString(
	QDataStream &stream,
	const QString &value);
[[nodiscard]] std::optional<QString> ReadString(QDataStream &stream);

} // namespace Binary
} // namespace MyOwnGram::MessageArchiveStorage
