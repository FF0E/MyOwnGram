/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "myowngram/activity_reporting_settings.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

class PeerData;

namespace MyOwnGram {

void RequestInteractiveStoryAction(
	std::shared_ptr<ChatHelpers::Show> show,
	ActivityReporting::StoryAction action,
	not_null<PeerData*> peer,
	Fn<void()> allowed,
	Fn<void()> denied = nullptr,
	bool viewerStyle = true);

} // namespace MyOwnGram
