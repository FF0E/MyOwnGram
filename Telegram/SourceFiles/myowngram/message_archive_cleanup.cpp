// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_cleanup.h"

#include "myowngram/message_archive_peer.h"
#include "storage/storage_account.h"

#include <QtCore/QDataStream>

#include <limits>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kLocalDeleteJobsVersion = uint16(1);
constexpr auto kLocalDeleteJobSize = int(
	sizeof(quint8)
	+ sizeof(quint8)
	+ sizeof(quint64)
	+ sizeof(quint64)
	+ sizeof(quint64)
	+ sizeof(qint64)
	+ sizeof(qint64)
	+ sizeof(quint64)
	+ sizeof(quint64)
	+ sizeof(quint64));

bool GoodAction(LocalDeleteAction action) {
	switch (action) {
	case LocalDeleteAction::Remove:
	case LocalDeleteAction::HistoryOnly:
		return true;
	}
	return false;
}

bool GoodJob(const LocalDeleteJob &job) {
	if (!GoodAction(job.action)
		|| !IsValidArchivePeerId(job.peer)
		|| job.before <= MsgId()
		|| job.before > ServerMaxMsgId
		|| !job.throughSequence) {
		return false;
	}
	switch (job.scope) {
	case LocalDeleteScope::All:
		return !job.from
			&& !job.sublistPeer
			&& !job.topicRootId
			&& !job.minDate
			&& !job.maxDate;
	case LocalDeleteScope::Dates:
		return !job.from
			&& !job.sublistPeer
			&& !job.topicRootId
			&& job.minDate < job.maxDate;
	case LocalDeleteScope::Participant:
		return IsValidArchivePeerId(job.from)
			&& !job.sublistPeer
			&& !job.topicRootId
			&& !job.minDate
			&& !job.maxDate;
	case LocalDeleteScope::Topic:
		return !job.from
			&& !job.sublistPeer
			&& IsServerMsgId(job.topicRootId)
			&& !job.minDate
			&& !job.maxDate;
	case LocalDeleteScope::Sublist:
		return !job.from
			&& IsValidArchivePeerId(job.sublistPeer)
			&& !job.topicRootId
			&& !job.minDate
			&& !job.maxDate;
	}
	return false;
}

ParsedLocalDeleteJobs ParseFailure(ParseError error) {
	return { .error = error };
}

} // namespace

std::optional<QByteArray> SerializeLocalDeleteJobs(
		const std::vector<LocalDeleteJob> &jobs) {
	if (jobs.empty()
		|| jobs.size() > size_t(
			(Storage::kMessageArchiveMaxRecordSize - sizeof(quint32))
			/ kLocalDeleteJobSize)) {
		return std::nullopt;
	}
	const auto count = int(jobs.size());
	auto payload = QByteArray();
	payload.reserve(int(sizeof(quint32)) + (count * kLocalDeleteJobSize));
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream << quint32(count);
	for (const auto &job : jobs) {
		if (!GoodJob(job)) {
			return std::nullopt;
		}
		const auto peer = SerializeArchivePeerId(job.peer);
		const auto from = job.from
			? SerializeArchivePeerId(job.from)
			: std::optional<uint64>(0);
		const auto sublistPeer = job.sublistPeer
			? SerializeArchivePeerId(job.sublistPeer)
			: std::optional<uint64>(0);
		if (!peer || !from || !sublistPeer) {
			return std::nullopt;
		}
		stream
			<< quint8(job.action)
			<< quint8(job.scope)
			<< quint64(*peer)
			<< quint64(job.before.bare)
			<< quint64(job.throughSequence)
			<< qint64(job.minDate)
			<< qint64(job.maxDate)
			<< quint64(*from)
			<< quint64(job.topicRootId.bare)
			<< quint64(*sublistPeer);
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::LocalDeleteJobs,
		kLocalDeleteJobsVersion,
		payload);
}

ParsedLocalDeleteJobs ParseLocalDeleteJobs(const QByteArray &serialized) {
	const auto record = ParseRecord(
		serialized,
		RecordType::LocalDeleteJobs,
		kLocalDeleteJobsVersion);
	if (!record) {
		return ParseFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto count = quint32();
	stream >> count;
	const auto available = stream.device()->bytesAvailable();
	if (stream.status() != QDataStream::Ok
		|| !count
		|| available < 0
		|| quint64(available) != (quint64(count) * kLocalDeleteJobSize)) {
		return ParseFailure(ParseError::Corrupt);
	}
	auto result = std::vector<LocalDeleteJob>();
	result.reserve(count);
	for (auto i = quint32(); i != count; ++i) {
		auto action = quint8();
		auto scope = quint8();
		auto serializedPeer = quint64();
		auto before = quint64();
		auto throughSequence = quint64();
		auto minDate = qint64();
		auto maxDate = qint64();
		auto serializedFrom = quint64();
		auto topicRootId = quint64();
		auto serializedSublistPeer = quint64();
		stream
			>> action
			>> scope
			>> serializedPeer
			>> before
			>> throughSequence
			>> minDate
			>> maxDate
			>> serializedFrom
			>> topicRootId
			>> serializedSublistPeer;
		if (stream.status() != QDataStream::Ok
			|| before > uint64(ServerMaxMsgId.bare)
			|| topicRootId >= uint64(ServerMaxMsgId.bare)
			|| minDate < std::numeric_limits<TimeId>::min()
			|| minDate > std::numeric_limits<TimeId>::max()
			|| maxDate < std::numeric_limits<TimeId>::min()
			|| maxDate > std::numeric_limits<TimeId>::max()) {
			return ParseFailure(ParseError::Corrupt);
		}
		const auto peer = DeserializeArchivePeerId(serializedPeer);
		auto from = std::optional<PeerId>();
		if (serializedFrom) {
			from = DeserializeArchivePeerId(serializedFrom);
			if (!from) {
				return ParseFailure(ParseError::Corrupt);
			}
		}
		auto sublistPeer = std::optional<PeerId>();
		if (serializedSublistPeer) {
			sublistPeer = DeserializeArchivePeerId(serializedSublistPeer);
			if (!sublistPeer) {
				return ParseFailure(ParseError::Corrupt);
			}
		}
		auto job = LocalDeleteJob{
			.peer = peer.value_or(PeerId()),
			.from = from.value_or(PeerId()),
			.sublistPeer = sublistPeer.value_or(PeerId()),
			.before = MsgId(int64(before)),
			.topicRootId = MsgId(int64(topicRootId)),
			.throughSequence = throughSequence,
			.minDate = TimeId(minDate),
			.maxDate = TimeId(maxDate),
			.action = LocalDeleteAction(action),
			.scope = LocalDeleteScope(scope),
		};
		if (!peer || !GoodJob(job)) {
			return ParseFailure(ParseError::Corrupt);
		}
		result.push_back(job);
	}
	if (!stream.atEnd()) {
		return ParseFailure(ParseError::Corrupt);
	}
	return {
		.value = std::move(result),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
void ValidateLocalDeleteJobsFormat() {
	static const auto validated = [] {
		const auto user = peerFromUser(UserId(1));
		const auto channel = peerFromChannel(ChannelId(2));
		const auto jobs = std::vector<LocalDeleteJob>{
			{
				.peer = user,
				.before = MsgId(600),
				.throughSequence = 1,
				.action = LocalDeleteAction::Remove,
				.scope = LocalDeleteScope::All,
			},
			{
				.peer = channel,
				.before = MsgId(500),
				.throughSequence = 2,
				.minDate = 10,
				.maxDate = 20,
				.action = LocalDeleteAction::HistoryOnly,
				.scope = LocalDeleteScope::Dates,
			},
			{
				.peer = channel,
				.from = user,
				.before = MsgId(400),
				.throughSequence = 3,
				.action = LocalDeleteAction::Remove,
				.scope = LocalDeleteScope::Participant,
			},
			{
				.peer = channel,
				.before = MsgId(300),
				.topicRootId = MsgId(10),
				.throughSequence = 4,
				.action = LocalDeleteAction::HistoryOnly,
				.scope = LocalDeleteScope::Topic,
			},
			{
				.peer = channel,
				.sublistPeer = user,
				.before = MsgId(200),
				.throughSequence = 5,
				.action = LocalDeleteAction::Remove,
				.scope = LocalDeleteScope::Sublist,
			},
		};
		const auto serialized = SerializeLocalDeleteJobs(jobs);
		Assert(serialized.has_value());
		const auto parsed = ParseLocalDeleteJobs(*serialized);
		Assert(parsed && parsed.value == jobs);
		Assert(!SerializeLocalDeleteJobs({}));
		auto invalid = jobs;
		invalid[0].from = user;
		Assert(!SerializeLocalDeleteJobs(invalid));
		invalid = jobs;
		invalid[0].before = MsgId();
		Assert(!SerializeLocalDeleteJobs(invalid));
		invalid = jobs;
		invalid[0].throughSequence = 0;
		Assert(!SerializeLocalDeleteJobs(invalid));
		const auto record = ParseRecord(
			*serialized,
			RecordType::LocalDeleteJobs,
			kLocalDeleteJobsVersion);
		Assert(record);
		auto invalidAction = record.payload;
		invalidAction[int(sizeof(quint32))] = char(0);
		const auto invalidActionRecord = SerializeRecord(
			RecordType::LocalDeleteJobs,
			kLocalDeleteJobsVersion,
			invalidAction);
		Assert(invalidActionRecord.has_value());
		Assert(!ParseLocalDeleteJobs(*invalidActionRecord));
		auto invalidScope = record.payload;
		invalidScope[int(sizeof(quint32)) + 1] = char(0);
		const auto invalidScopeRecord = SerializeRecord(
			RecordType::LocalDeleteJobs,
			kLocalDeleteJobsVersion,
			invalidScope);
		Assert(invalidScopeRecord.has_value());
		Assert(!ParseLocalDeleteJobs(*invalidScopeRecord));
		const auto future = SerializeRecord(
			RecordType::LocalDeleteJobs,
			kLocalDeleteJobsVersion + 1,
			record.payload);
		Assert(future.has_value());
		Assert(ParseLocalDeleteJobs(*future).error
			== ParseError::UnsupportedVersion);
		Assert(!ParseLocalDeleteJobs(serialized->chopped(1)));
		return true;
	}();
	Q_UNUSED(validated);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
