/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MyOwnGram::MiniApps {

enum class OpenMode {
	Internal,
	Ask,
	Browser,
};

[[nodiscard]] OpenMode Mode();
[[nodiscard]] rpl::producer<OpenMode> ModeChanges();
void SetMode(OpenMode mode);

} // namespace MyOwnGram::MiniApps
