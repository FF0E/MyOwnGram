// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "data/data_peer_id.h"

#include <optional>

namespace MyOwnGram::MessageArchiveStorage {

[[nodiscard]] inline bool IsValidArchivePeerId(PeerId id) {
	const auto bare = id.value & PeerId::kChatTypeMask;
	if (!bare) {
		return false;
	}
	switch (id.value >> 48) {
	case UserId::kShift:
	case ChatId::kShift:
	case ChannelId::kShift:
		return true;
	}
	return false;
}

[[nodiscard]] inline std::optional<uint64> SerializeArchivePeerId(
		PeerId id) {
	return IsValidArchivePeerId(id)
		? std::optional<uint64>(SerializePeerId(id))
		: std::nullopt;
}

[[nodiscard]] inline std::optional<PeerId> DeserializeArchivePeerId(
		uint64 serialized) {
	const auto modernFlag = uint64(UserId::kReservedBit) << 48;
	if (!(serialized & modernFlag)) {
		return std::nullopt;
	}
	const auto value = serialized & ~modernFlag;
	const auto bare = value & PeerId::kChatTypeMask;
	if (!bare) {
		return std::nullopt;
	}
	auto id = PeerId();
	switch (value >> 48) {
	case UserId::kShift:
		id = peerFromUser(UserId(bare));
		break;
	case ChatId::kShift:
		id = peerFromChat(ChatId(bare));
		break;
	case ChannelId::kShift:
		id = peerFromChannel(ChannelId(bare));
		break;
	default:
		return std::nullopt;
	}
	return SerializePeerId(id) == serialized
		? std::optional<PeerId>(id)
		: std::nullopt;
}

} // namespace MyOwnGram::MessageArchiveStorage
