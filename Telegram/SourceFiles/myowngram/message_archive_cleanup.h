// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "data/data_msg_id.h"
#include "myowngram/message_archive_record.h"

#include <optional>
#include <vector>

namespace MyOwnGram::MessageArchiveStorage {

enum class LocalDeleteAction : uint8 {
	Remove = 1,
	HistoryOnly = 2,
};

enum class LocalDeleteScope : uint8 {
	All = 1,
	Dates = 2,
	Participant = 3,
	Topic = 4,
	Sublist = 5,
};

struct LocalDeleteJob {
	PeerId peer;
	PeerId from;
	PeerId sublistPeer;
	MsgId before;
	MsgId topicRootId;
	uint64 throughSequence = 0;
	TimeId minDate = 0;
	TimeId maxDate = 0;
	LocalDeleteAction action = LocalDeleteAction::Remove;
	LocalDeleteScope scope = LocalDeleteScope::All;

	friend inline bool operator==(
		const LocalDeleteJob &,
		const LocalDeleteJob &) = default;
};

struct ParsedLocalDeleteJobs {
	std::vector<LocalDeleteJob> value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

[[nodiscard]] std::optional<QByteArray> SerializeLocalDeleteJobs(
	const std::vector<LocalDeleteJob> &jobs);
[[nodiscard]] ParsedLocalDeleteJobs ParseLocalDeleteJobs(
	const QByteArray &serialized);

#ifdef _DEBUG
void ValidateLocalDeleteJobsFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
