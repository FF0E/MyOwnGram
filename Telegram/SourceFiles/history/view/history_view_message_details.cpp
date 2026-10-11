/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_details.h"

#include "apiwrap.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/scheduled_messages.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/details/mtproto_dump_to_text.h"
#include "mtproto/mtproto_response.h"
#include "ui/widgets/popup_menu.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

using RemoteRequest = std::variant<
	MTPmessages_GetMessages,
	MTPchannels_GetMessages,
	MTPmessages_GetScheduledMessages,
	MTPmessages_GetQuickReplyMessages>;

std::optional<RemoteRequest> RequestFor(not_null<HistoryItem*> item) {
	if (item->isSending()
		|| item->hasFailed()
		|| item->isSponsored()
		|| item->isFakeAboutView()) {
		return {};
	} else if (item->isScheduled()) {
		const auto id = item->history()->session().scheduledMessages().lookupId(
			item);
		return MTPmessages_GetScheduledMessages(
			item->history()->peer->input(),
			MTP_vector<MTPint>(1, MTP_int(id)));
	} else if (item->isBusinessShortcut()) {
		const auto id = item->history()->owner().shortcutMessages().lookupId(
			item);
		return MTPmessages_GetQuickReplyMessages(
			MTP_flags(MTPmessages_GetQuickReplyMessages::Flag::f_id),
			MTP_int(item->shortcutId()),
			MTP_vector<MTPint>(1, MTP_int(id)),
			MTP_long(0));
	} else if (!item->isRegular()) {
		return {};
	} else if (const auto channel = item->history()->peer->asChannel()) {
		return MTPchannels_GetMessages(
			channel->inputChannel(),
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(item->id))));
	}
	return MTPmessages_GetMessages(MTP_vector<MTPInputMessage>(
		1,
		MTP_inputMessageID(MTP_int(item->id))));
}

void CopyMessageDetails(
		std::shared_ptr<Main::SessionShow> show,
		const RemoteRequest &request) {
	const auto session = &show->session();
	v::match(request, [&](auto method) {
		session->api().request(std::move(method)).done([=](
				const MTPmessages_Messages &result) {
			QGuiApplication::clipboard()->setText(
				MTP::details::DumpObject(result));
			show->showToast(tr::lng_text_copied(tr::now));
		}).fail([=](const MTP::Error &error) {
			MTP::ShowErrorFallback(show, error);
		}).handleFloodErrors().send();
	});
}

} // namespace

void AddMessageDetailsAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		std::shared_ptr<Main::SessionShow> show) {
	const auto request = RequestFor(item);
	if (!request) {
		return;
	}
	menu->addAction(
		tr::lng_myowngram_copy_message_details(tr::now),
		crl::guard(&show->session(), [=] {
			CopyMessageDetails(show, *request);
		}),
		&st::menuIconCopy);
}

} // namespace HistoryView
