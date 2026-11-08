/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

void ShowMessageHistory(
	not_null<Window::SessionController*> controller,
	PeerId peer,
	MsgId message = {});
void RemoveMessageHistory(
	not_null<Window::SessionController*> controller,
	FullMsgId message,
	qint64 recordId = 0);

} // namespace HistoryView
