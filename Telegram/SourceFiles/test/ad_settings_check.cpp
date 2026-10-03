/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/ad_settings.h"

#include <cassert>
#include <iostream>

#ifdef NDEBUG
#error "Run this check with assertions enabled."
#endif // NDEBUG

int main() {
	static_assert(Core::kAdPlacements.size() == 4);
	const auto defaults = Core::AdPreferences();
	for (auto i = std::size_t(); i != defaults.size(); ++i) {
		assert(std::size_t(Core::kAdPlacements[i]) == i);
		assert(defaults[i].get && defaults[i].show);
	}
	for (auto bits = 0; bits != 256; ++bits) {
		auto preferences = Core::AdPreferences();
		for (auto i = std::size_t(); i != preferences.size(); ++i) {
			preferences[i] = {
				bool(bits & (1 << (2 * i))),
				bool(bits & (1 << (2 * i + 1))),
			};
		}
		const auto bytes = Core::SerializeAdPreferences(preferences);
		for (auto i = std::size_t(); i != bytes.size(); ++i) {
			assert(bytes[i] == char((bits >> (2 * i)) & 3));
		}
		assert(Core::DeserializeAdPreferences({ bytes.data(), bytes.size() })
			== preferences);
	}
	const auto wrongLength = std::array<char, 5>();
	for (auto size = std::size_t(); size <= wrongLength.size(); ++size) {
		if (size != defaults.size()) {
			assert(Core::DeserializeAdPreferences({ wrongLength.data(), size })
				== defaults);
		}
	}
	for (auto i = std::size_t(); i != defaults.size(); ++i) {
		auto bytes = Core::SerializeAdPreferences(defaults);
		for (auto invalid = 4; invalid != 256; ++invalid) {
			bytes[i] = char(invalid);
			assert(Core::DeserializeAdPreferences({ bytes.data(), bytes.size() })
				== defaults);
		}
	}
	std::cout << "Ad settings: all 256 combinations and malformed values passed.\n";
}
