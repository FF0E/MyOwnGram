/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_text.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/image/image_location.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include <rpl/after_next.h>
#include <rpl/before_next.h>
#include <rpl/filter.h>

#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_info_profile_actions.h"

namespace Info::Profile {
namespace {

QString PeerIdText(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Core::PeerIdDisplay mode) {
	if (mode == Core::PeerIdDisplay::Hidden) {
		return QString();
	} else if (topicRootId) {
		return QString::number(topicRootId.bare);
	}
	auto id = int64(peer->id.value & PeerId::kChatTypeMask);
	if (mode == Core::PeerIdDisplay::BotApi) {
		if (peer->isChannel()) {
			id = -1000000000000LL - id;
		} else if (peer->isChat()) {
			id = -id;
		}
	}
	return QString::number(id);
}

int PeerDcId(not_null<PeerData*> peer) {
	if (const auto dc = peer->owner().statsDcId(peer)) {
		return dc;
	}
	const auto location = peer->userpicLocation();
	const auto file = std::get_if<StorageFileLocation>(&location.file().data);
	return file ? file->dcId() : 0;
}

rpl::producer<QString> DcTextValue(int dc) {
	auto location = rpl::producer<QString>();
	switch (dc) {
	case 1:
	case 3:
		location = tr::lng_myowngram_dc_miami();
		break;
	case 2:
	case 4:
		location = tr::lng_myowngram_dc_amsterdam();
		break;
	case 5:
		location = tr::lng_myowngram_dc_singapore();
		break;
	default:
		return rpl::single(dc ? u"DC%1"_q.arg(dc) : QString());
	}
	return std::move(location) | rpl::map([=](const QString &name) {
		return u"DC%1, %2"_q.arg(dc).arg(name);
	});
}

} // namespace

object_ptr<Ui::SlideWrap<Ui::VerticalLayout>> CreatePeerIdRow(
		QWidget *parent,
		not_null<PeerData*> peer,
		std::shared_ptr<Main::SessionShow> show,
		MsgId topicRootId) {
	const auto settings = &Core::App().settings();
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent),
		st::infoProfileLabeledPadding);
	result->setDuration(st::infoSlideDuration);
	const auto layout = result->entity();
	const auto label = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		settings->peerIdDisplayValue() | rpl::map([=](Core::PeerIdDisplay mode) {
			return Ui::Text::Code(PeerIdText(peer, topicRootId, mode));
		}),
		st::infoLabeledOneLine));
	label->setSelectable(true);
	label->setDoubleClickSelectsParagraph(true);
	label->setContextCopyText(tr::lng_myowngram_copy_id(tr::now));
	label->overrideLinkClickHandler([=] {
		TextUtilities::SetClipboardText({
			PeerIdText(peer, topicRootId, settings->peerIdDisplay()),
		});
		show->showToast(tr::lng_text_copied(tr::now));
	});
	if (!topicRootId) {
		const auto caption = layout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				layout,
				object_ptr<Ui::VerticalLayout>(layout)));
		const auto inner = caption->entity();
		inner->add(Ui::CreateSkipWidget(inner, st::infoLabelSkip));
		auto dc = peer->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::Photo | Data::PeerUpdate::Flag::FullInfo
		) | rpl::map([=] {
			return PeerDcId(peer);
		}) | rpl::distinct_until_changed()
			| rpl::start_spawning(caption->lifetime());
		inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			rpl::duplicate(dc) | rpl::map(DcTextValue) | rpl::flatten_latest(),
			st::infoLabel));
		caption->toggleOn(std::move(dc) | rpl::map(rpl::mappers::_1 != 0));
		caption->finishAnimating();
	}
	result->toggleOn(settings->peerIdDisplayValue() | rpl::map(
		rpl::mappers::_1 != Core::PeerIdDisplay::Hidden));
	result->finishAnimating();
	return result;
}

TextWithLabel CreateTextWithLabel(
		QWidget *parent,
		rpl::producer<TextWithEntities> &&label,
		rpl::producer<TextWithEntities> &&text,
		const style::FlatLabel &labelSt,
		const style::FlatLabel &textSt,
		const style::margins &padding,
		const style::PopupMenu &stMenu) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent),
		padding);
	result->setDuration(st::infoSlideDuration);
	auto layout = result->entity();
	auto nonEmptyText = rpl::duplicate(
		text
	) | rpl::before_next([slide = result.data()](
			const TextWithEntities &value) {
		if (value.text.isEmpty()) {
			slide->hide(anim::type::normal);
		}
	}) | rpl::filter([](const TextWithEntities &value) {
		return !value.text.isEmpty();
	}) | rpl::after_next([slide = result.data()](
			const TextWithEntities &value) {
		slide->show(anim::type::normal);
	});
	const auto labeled = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(nonEmptyText),
		textSt,
		stMenu));
	std::move(text) | rpl::on_next([=] {
		labeled->resizeToWidth(layout->widthNoMargins());
	}, labeled->lifetime());
	labeled->setSelectable(true);
	layout->add(Ui::CreateSkipWidget(layout, st::infoLabelSkip));
	const auto subtext = layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(
			label
		) | rpl::after_next([=] {
			layout->resizeToWidth(layout->widthNoMargins());
		}),
		labelSt));
	result->finishAnimating();
	return { std::move(result), labeled, subtext };
}

} // namespace Info::Profile
