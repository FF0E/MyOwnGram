/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MyOwnGram::ActivityReporting {

[[nodiscard]] bool SendTypingStatus();
[[nodiscard]] rpl::producer<bool> SendTypingStatusChanges();
void SetSendTypingStatus(bool enabled);

[[nodiscard]] bool SendReadMetrics();
[[nodiscard]] rpl::producer<bool> SendReadMetricsChanges();
void SetSendReadMetrics(bool enabled);

} // namespace MyOwnGram::ActivityReporting
