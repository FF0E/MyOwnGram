// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_index.h"

#include "data/data_msg_id.h"
#include "data/data_peer_id.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

// Seven byte-wide radix levels cover a 56-bit server message ID.
// Each prefix-keyed node is a canonical bitmap of at most 32 bytes,
// keeping sparse private-chat IDs compact while allowing chronological
// traversal without database key iteration or per-chat limits.
constexpr auto kMessagePositionIndexVersion = uint16(1);
constexpr auto kMessagePositionNodeBits = 1 << 8;
static_assert(uint8(RecordType::MessagePositionIndex) == 12);

struct ParsedMessagePositionBits {
	QByteArray value;
	ParseError error = ParseError::Corrupt;

	explicit operator bool() const {
		return error == ParseError::None;
	}
};

ParsedMessagePositionBits ParseMessagePositionBits(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return { .error = ParseError::None };
	}
	auto record = ParseRecord(
		serialized,
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion);
	const auto byteLimit = (kMessagePositionNodeBits + 7) / 8;
	if (!record) {
		return { .error = record.error };
	} else if (record.payload.size() > byteLimit
		|| (!record.payload.isEmpty() && record.payload.back() == 0)) {
		return {};
	}
	return {
		.value = std::move(record.payload),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
std::optional<bool> MessagePositionContains(
		const QByteArray &serialized,
		const MessagePositionEntry &entry) {
	if (!entry.key.valid()) {
		return std::nullopt;
	}
	const auto found = FindMessagePosition(serialized, entry.bit);
	return found
		? std::make_optional(found.found && found.bit == entry.bit)
		: std::nullopt;
}

void CheckMessagePositionIndexFormat() {
	const auto id = FullMsgId(
		peerFromUser(UserId(123)),
		MsgId(int64(0x00AB'CDEF'1234'5678ULL)));
	const auto path = MessagePositionPath(id);
	constexpr auto bits = std::array<uint8, kMessagePositionLevels>{
		0xAB,
		0xCD,
		0xEF,
		0x12,
		0x34,
		0x56,
		0x78,
	};
	constexpr auto positionMask = (uint64(1) << 56) - 1;
	constexpr auto levelShift = 48;
	const auto position = uint64(id.msg.bare);
	for (auto level = 0; level != int(path.size()); ++level) {
		const auto prefixBits = level * 8;
		const auto prefix = prefixBits
			? (position >> (56 - prefixBits))
			: 0;
		Assert(path[level].bit == bits[level]);
		Assert(uint8(path[level].key.low >> 56)
			== uint8(RecordType::MessagePositionIndex));
		Assert((path[level].key.low & positionMask)
			== ((uint64(level) << levelShift) | prefix));
		Assert(path[level].key.high == path.front().key.high);
	}
	const auto maximumPath = MessagePositionPath(FullMsgId(
		id.peer,
		ServerMaxMsgId - 1));
	for (const auto &entry : maximumPath) {
		Assert(entry.bit == 0xFF);
	}

	for (const auto &entry : path) {
		const auto added = AddMessagePosition({}, entry);
		Assert(added.result == MessagePositionUpdateResult::Updated);
		const auto present = MessagePositionContains(added.value, entry);
		Assert(present && *present);
		const auto duplicate = AddMessagePosition(added.value, entry);
		Assert(duplicate.result == MessagePositionUpdateResult::Unchanged);
	}

	const auto empty = FindMessagePosition({}, 0xFF);
	Assert(empty && !empty.found);

	const auto first = path.back();
	auto second = first;
	second.bit = uint8(first.bit + 1);
	const auto firstAdded = AddMessagePosition({}, first);
	const auto secondAdded = AddMessagePosition(firstAdded.value, second);
	Assert(firstAdded.result == MessagePositionUpdateResult::Updated);
	Assert(secondAdded.result == MessagePositionUpdateResult::Updated);
	const auto firstPresent = MessagePositionContains(
		secondAdded.value,
		first);
	const auto secondPresent = MessagePositionContains(
		secondAdded.value,
		second);
	Assert(firstPresent && *firstPresent);
	Assert(secondPresent && *secondPresent);
	const auto foundSecond = FindMessagePosition(secondAdded.value, 0xFF);
	Assert(foundSecond && foundSecond.found && foundSecond.bit == second.bit);
	const auto foundFirst = FindMessagePosition(secondAdded.value, first.bit);
	Assert(foundFirst && foundFirst.found && foundFirst.bit == first.bit);
	const auto beforeFirst = FindMessagePosition(
		secondAdded.value,
		uint8(first.bit - 1));
	Assert(beforeFirst && !beforeFirst.found);

	const auto firstRemoved = RemoveMessagePosition(secondAdded.value, first);
	Assert(firstRemoved.result == MessagePositionUpdateResult::Updated);
	Assert(!firstRemoved.empty);
	const auto firstAfterRemoval = MessagePositionContains(
		firstRemoved.value,
		first);
	const auto secondAfterRemoval = MessagePositionContains(
		firstRemoved.value,
		second);
	Assert(firstAfterRemoval && !*firstAfterRemoval);
	Assert(secondAfterRemoval && *secondAfterRemoval);
	const auto secondRemoved = RemoveMessagePosition(
		firstRemoved.value,
		second);
	Assert(secondRemoved.result == MessagePositionUpdateResult::Updated);
	Assert(secondRemoved.empty && secondRemoved.value.isEmpty());
	const auto missingRemoved = RemoveMessagePosition({}, first);
	Assert(missingRemoved.result == MessagePositionUpdateResult::Unchanged);
	Assert(missingRemoved.empty);

	auto highest = path.back();
	highest.bit = 0xFF;
	const auto highestAdded = AddMessagePosition({}, highest);
	Assert(highestAdded.result == MessagePositionUpdateResult::Updated);
	const auto highestParsed = ParseRecord(
		highestAdded.value,
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion);
	Assert(highestParsed);
	Assert(highestParsed.payload.size()
		== (kMessagePositionNodeBits / 8));
	const auto highestPresent = MessagePositionContains(
		highestAdded.value,
		highest);
	Assert(highestPresent && *highestPresent);

	Assert(AddMessagePosition({}, MessagePositionEntry()).result
		== MessagePositionUpdateResult::Invalid);
	Assert(RemoveMessagePosition({}, MessagePositionEntry()).result
		== MessagePositionUpdateResult::Invalid);

	const auto trailingZero = SerializeRecord(
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion,
		QByteArray(1, 0));
	Assert(trailingZero.has_value());
	Assert(!MessagePositionContains(*trailingZero, path.front()));
	Assert(!FindMessagePosition(*trailingZero, 0xFF));
	Assert(RemoveMessagePosition(*trailingZero, path.front()).result
		== MessagePositionUpdateResult::Invalid);

	const auto oversized = SerializeRecord(
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion,
		QByteArray((kMessagePositionNodeBits / 8) + 1, 1));
	Assert(oversized.has_value());
	Assert(!MessagePositionContains(*oversized, path.front()));

	const auto future = SerializeRecord(
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion + 1,
		QByteArray(1, 1));
	Assert(future.has_value());
	Assert(!MessagePositionContains(*future, path.front()));
	Assert(FindMessagePosition(*future, 0xFF).error
		== ParseError::UnsupportedVersion);

	const auto wrongType = SerializeRecord(
		RecordType::MessageTimeline,
		kMessagePositionIndexVersion,
		QByteArray(1, 1));
	Assert(wrongType.has_value());
	Assert(!MessagePositionContains(*wrongType, path.front()));
	Assert(FindMessagePosition(*wrongType, 0xFF).error
		== ParseError::WrongType);
}
#endif // _DEBUG

} // namespace

MessagePositionLookup FindMessagePosition(
		const QByteArray &serialized,
		uint8 till) {
	const auto parsed = ParseMessagePositionBits(serialized);
	if (!parsed) {
		return { .error = parsed.error };
	}
	for (auto byte = std::min(till / 8, int(parsed.value.size()) - 1);
			byte >= 0;
			--byte) {
		auto value = uint8(parsed.value[byte]);
		if (byte == (till / 8)) {
			value &= uint8((uint16(1) << ((till % 8) + 1)) - 1);
		}
		for (auto bit = 7; bit >= 0; --bit) {
			if (value & (1U << bit)) {
				return {
					.error = ParseError::None,
					.bit = uint8((byte * 8) + bit),
					.found = true,
				};
			}
		}
	}
	return { .error = ParseError::None };
}

MessagePositionUpdate AddMessagePosition(
		const QByteArray &serialized,
		const MessagePositionEntry &entry) {
	if (!entry.key.valid()) {
		return {};
	}
	auto parsed = ParseMessagePositionBits(serialized);
	if (!parsed) {
		return {};
	}
	const auto byte = int(entry.bit / 8);
	const auto mask = uint8(1U << (entry.bit % 8));
	if (byte < parsed.value.size()
		&& (uint8(parsed.value[byte]) & mask)) {
		return {
			.result = MessagePositionUpdateResult::Unchanged,
		};
	}
	if (parsed.value.size() <= byte) {
		parsed.value.append(QByteArray(byte + 1 - parsed.value.size(), 0));
	}
	parsed.value[byte] = char(uint8(parsed.value[byte]) | mask);
	auto result = SerializeRecord(
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion,
		parsed.value);
	return result
		? MessagePositionUpdate{
			.value = std::move(*result),
			.result = MessagePositionUpdateResult::Updated,
		}
		: MessagePositionUpdate();
}

MessagePositionUpdate RemoveMessagePosition(
		const QByteArray &serialized,
		const MessagePositionEntry &entry) {
	if (!entry.key.valid()) {
		return {};
	}
	auto parsed = ParseMessagePositionBits(serialized);
	if (!parsed) {
		return {};
	}
	const auto byte = int(entry.bit / 8);
	const auto mask = uint8(1U << (entry.bit % 8));
	if (byte >= parsed.value.size()
		|| !(uint8(parsed.value[byte]) & mask)) {
		return {
			.result = MessagePositionUpdateResult::Unchanged,
			.empty = parsed.value.isEmpty(),
		};
	}
	parsed.value[byte] = char(uint8(parsed.value[byte]) & ~mask);
	while (!parsed.value.isEmpty() && parsed.value.back() == 0) {
		parsed.value.chop(1);
	}
	if (parsed.value.isEmpty()) {
		return {
			.result = MessagePositionUpdateResult::Updated,
			.empty = true,
		};
	}
	auto result = SerializeRecord(
		RecordType::MessagePositionIndex,
		kMessagePositionIndexVersion,
		parsed.value);
	return result
		? MessagePositionUpdate{
			.value = std::move(*result),
			.result = MessagePositionUpdateResult::Updated,
		}
		: MessagePositionUpdate();
}

#ifdef _DEBUG
void ValidateMessagePositionIndexFormat() {
	static auto checked = false;
	if (!checked) {
		checked = true;
		CheckMessagePositionIndexFormat();
	}
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
