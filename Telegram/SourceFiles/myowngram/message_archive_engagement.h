// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "data/data_message_reaction_id.h"
#include "data/data_peer_id.h"
#include "myowngram/message_archive_record.h"

#include <optional>
#include <vector>

class HistoryItem;

namespace MyOwnGram::MessageArchiveStorage {

struct DeletedMessageViews {
	int views = -1;
	int replies = -1;
	int forwards = -1;
	std::vector<PeerId> recentRepliers;
	ChannelId commentsMegagroupId = 0;
	MsgId commentsRootId = 0;

	friend inline bool operator==(
		const DeletedMessageViews &,
		const DeletedMessageViews &) = default;
};

struct DeletedMessageReaction {
	Data::ReactionId id;
	int count = 0;
	bool my = false;

	friend inline bool operator==(
		const DeletedMessageReaction &,
		const DeletedMessageReaction &) = default;
};

struct DeletedMessageRecentReaction {
	PeerId peer;
	bool my = false;

	friend inline bool operator==(
		const DeletedMessageRecentReaction &,
		const DeletedMessageRecentReaction &) = default;
};

struct DeletedMessageRecentReactions {
	Data::ReactionId id;
	std::vector<DeletedMessageRecentReaction> list;

	friend inline bool operator==(
		const DeletedMessageRecentReactions &,
		const DeletedMessageRecentReactions &) = default;
};

struct DeletedMessageTopPaidReaction {
	PeerId peer;
	int count = 0;
	bool top = false;
	bool my = false;

	friend inline bool operator==(
		const DeletedMessageTopPaidReaction &,
		const DeletedMessageTopPaidReaction &) = default;
};

struct DeletedMessageEngagement {
	std::optional<DeletedMessageViews> views;
	std::vector<DeletedMessageReaction> reactions;
	std::vector<DeletedMessageRecentReactions> recentReactions;
	std::vector<DeletedMessageTopPaidReaction> topPaidReactions;
	bool canViewReactions = false;

	friend inline bool operator==(
		const DeletedMessageEngagement &,
		const DeletedMessageEngagement &) = default;
};

struct ParsedDeletedMessageEngagement {
	DeletedMessageEngagement value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] std::optional<QByteArray> SerializeDeletedMessageEngagement(
	const DeletedMessageEngagement &engagement);
[[nodiscard]] std::optional<QByteArray> SerializeDeletedMessageEngagement(
	not_null<const HistoryItem*> item);
[[nodiscard]] ParsedDeletedMessageEngagement ParseDeletedMessageEngagement(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateDeletedMessageEngagementFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
