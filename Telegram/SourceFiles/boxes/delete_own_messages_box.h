/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

class PeerData;

namespace Ui {
class GenericBox;
} // namespace Ui

void DeleteOwnMessagesBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer,
	MsgId replyRootId);
