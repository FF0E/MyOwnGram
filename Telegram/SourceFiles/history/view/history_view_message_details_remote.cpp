/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_details_remote.h"

#include "apiwrap.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/scheduled_messages.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/details/mtproto_dump_to_text.h"
#include "mtproto/mtproto_response.h"
#include "ui/widgets/popup_menu.h"

#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>

#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

using RemoteRequest = std::variant<
	MTPmessages_GetMessages,
	MTPchannels_GetMessages,
	MTPmessages_GetScheduledMessages,
	MTPmessages_GetQuickReplyMessages>;

std::optional<RemoteRequest> RequestFor(not_null<HistoryItem*> item) {
	if (item->isSending() || item->hasFailed()) {
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

void FillRemoteMenu(not_null<Ui::PopupMenu*> menu, const QString &text) {
	menu->clearActions();
	menu->addAction(
		tr::lng_myowngram_details_remote_title(tr::now),
		[] {})->setEnabled(false);
	menu->addAction(
		tr::lng_myowngram_details_copy_remote(tr::now),
		[text] { QGuiApplication::clipboard()->setText(text); });
	menu->addSeparator();
	const auto &st = menu->st().menu;
	const auto width = st.widthMax - st.itemPadding.left()
		- st.itemPadding.right() - st.itemRightSkip;
	for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
		const auto label = st.itemStyle.font->elided(line, width)
			.replace('&', u"&&"_q);
		menu->addAction(label, [line] {
			QGuiApplication::clipboard()->setText(line);
		});
	}
}

void ShowRemoteDetails(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const RemoteRequest &request) {
	const auto menu = new Ui::PopupMenu(parent, st::messageDetailsPopup);
	const auto position = QCursor::pos();
	menu->deleteOnHide(true);
	menu->addAction(tr::lng_contacts_loading(tr::now), [] {})->setEnabled(false);
	menu->popup(position);
	const auto done = crl::guard(menu, [=](const MTPmessages_Messages &result) {
		FillRemoteMenu(menu, MTP::details::DumpObject(result));
		if (menu->prepareGeometryFor(position)) {
			menu->update();
		}
	});
	const auto fail = crl::guard(menu, [=](const MTP::Error &error) {
		if (MTP::IgnoreError(error)) {
			menu->hideMenu();
			return;
		}
		menu->clearActions();
		menu->addAction(
			tr::lng_myowngram_details_remote_error(tr::now),
			[] {})->setEnabled(false);
		menu->addAction(error.type(), [text = error.type()] {
			QGuiApplication::clipboard()->setText(text);
		});
		if (menu->prepareGeometryFor(position)) {
			menu->update();
		}
	});
	const auto id = v::match(request, [&](const auto &method) {
		return session->api().request(method).done(done).fail(fail)
			.handleFloodErrors().send();
	});
	menu->lifetime().add(crl::guard(session, [=] {
		session->api().request(id).cancel();
	}));
}

} // namespace

void AddRemoteMessageDetailsAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		QWidget *parent) {
	const auto request = RequestFor(item);
	if (!request) {
		return;
	}
	const auto session = &item->history()->session();
	menu->addAction(
		tr::lng_myowngram_details_fetch_remote(tr::now),
		crl::guard(session, [=, parent = QPointer<QWidget>(parent)] {
			if (parent) {
				ShowRemoteDetails(parent.data(), session, *request);
			}
		}));
}

} // namespace HistoryView
