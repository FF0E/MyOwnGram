/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "styles/style_widgets.h"

#include <rpl/producer.h>

class PeerData;

namespace Main {
class SessionShow;
} // namespace Main

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class VerticalLayout;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info::Profile {

struct TextWithLabel {
	object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> wrap;
	not_null<Ui::FlatLabel*> text;
	not_null<Ui::FlatLabel*> subtext;
};

object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> CreatePeerIdRow(
	QWidget *parent,
	not_null<PeerData*> peer,
	std::shared_ptr<Main::SessionShow> show,
	MsgId topicRootId = 0);

TextWithLabel CreateTextWithLabel(
	QWidget *parent,
	rpl::producer<TextWithEntities> &&label,
	rpl::producer<TextWithEntities> &&text,
	const style::FlatLabel &labelSt,
	const style::FlatLabel &textSt,
	const style::margins &padding,
	const style::PopupMenu &stMenu = st::defaultPopupMenu);

} // namespace Info::Profile
