// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"
#include "base/flags.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "myowngram/message_archive_record.h"
#include "ui/text/text_entity.h"

#include <optional>
#include <vector>

namespace MyOwnGram::MessageArchiveStorage {

enum class MessageTimelineFlag : uint8 {
	Deleted = 0x01,
	Expired = 0x02,
	HistoryOnly = 0x04,
};
inline constexpr bool is_flag_type(MessageTimelineFlag) { return true; }
using MessageTimelineFlags = base::flags<MessageTimelineFlag>;

struct MessageSnapshot {
	TimeId versionDate = 0;
	bool service = false;
	TextWithEntities text;
	QByteArray media;
	QByteArray replyMarkup;
	QByteArray support;

	friend inline bool operator==(
		const MessageSnapshot &,
		const MessageSnapshot &) = default;
};

struct MessageTimelineOrigin {
	PeerId from;
	TimeId date = 0;
	MsgId topicRootId;
	PeerId sublistPeer;
	uint64 archiveSequence = 0;

	friend inline bool operator==(
		const MessageTimelineOrigin &,
		const MessageTimelineOrigin &) = default;
};

struct MessageTimeline {
	MessageTimelineFlags flags;
	MessageTimelineOrigin origin;
	std::vector<MessageSnapshot> versions;

	friend inline bool operator==(
		const MessageTimeline &,
		const MessageTimeline &) = default;
};

enum class ObserveResult : uint8 {
	Unchanged,
	Refreshed,
	Appended,
};

struct MessageTimelineMetadata {
	MessageTimelineFlags flags;
	MessageTimelineOrigin origin;
};

struct ParsedMessageTimelineMetadata {
	MessageTimelineMetadata value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

struct ParsedMessageTimeline {
	MessageTimeline value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] bool SameVisibleContent(
	const MessageSnapshot &a,
	const MessageSnapshot &b);
ObserveResult ObserveVersion(
	MessageTimeline &timeline,
	MessageSnapshot snapshot);
[[nodiscard]] std::optional<QByteArray> SerializeMessageTimeline(
	const MessageTimeline &timeline);
[[nodiscard]] ParsedMessageTimelineMetadata ParseMessageTimelineMetadata(
	const QByteArray &serialized);
[[nodiscard]] ParsedMessageTimeline ParseMessageTimeline(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateMessageTimelineFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
