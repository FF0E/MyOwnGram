// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "storage/cache/storage_cache_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

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
};

enum class ParseError : uint8 {
	None,
	Corrupt,
	WrongType,
	UnsupportedVersion,
};

struct ParsedRecord {
	uint16 version = 0;
	ParseError error = ParseError::Corrupt;
	QByteArray payload;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] Storage::Cache::Key MessageTimelineKey(FullMsgId id);
[[nodiscard]] std::optional<QByteArray> SerializeRecord(
	RecordType type,
	uint16 version,
	const QByteArray &payload);
[[nodiscard]] ParsedRecord ParseRecord(
	const QByteArray &serialized,
	RecordType expectedType,
	uint16 latestVersion);

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
