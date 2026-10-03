/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <array>
#include <string_view>

namespace Core {

enum class AdPlacement {
	Channel,
	Bot,
	Search,
	Video,
};

inline constexpr auto kAdPlacements = std::array{
	AdPlacement::Channel,
	AdPlacement::Bot,
	AdPlacement::Search,
	AdPlacement::Video,
};

struct AdSettings {
	bool get = true;
	bool show = true;

	friend constexpr bool operator==(AdSettings, AdSettings) = default;
};

using AdPreferences = std::array<AdSettings, kAdPlacements.size()>;
using SerializedAdPreferences = std::array<char, kAdPlacements.size()>;

[[nodiscard]] constexpr SerializedAdPreferences SerializeAdPreferences(
		const AdPreferences &preferences) {
	auto result = SerializedAdPreferences();
	for (auto i = std::size_t(); i != result.size(); ++i) {
		result[i] = char((preferences[i].get ? 1 : 0)
			| (preferences[i].show ? 2 : 0));
	}
	return result;
}

[[nodiscard]] constexpr AdPreferences DeserializeAdPreferences(
		std::string_view data) {
	if (data.size() != kAdPlacements.size()) {
		return {};
	}
	auto result = AdPreferences();
	for (auto i = std::size_t(); i != result.size(); ++i) {
		const auto flags = static_cast<unsigned char>(data[i]);
		if (flags > 3) {
			return {};
		}
		result[i] = { bool(flags & 1), bool(flags & 2) };
	}
	return result;
}

} // namespace Core
