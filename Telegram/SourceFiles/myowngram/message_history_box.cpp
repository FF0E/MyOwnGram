// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_history_box.h"

#include "core/ui_integration.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "myowngram/message_archive.h"
#include "myowngram/message_archive_snapshot.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_extended_data.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace MyOwnGram {
namespace {

using MessageArchiveStorage::MessageTimeline;
using MessageArchiveStorage::MessageTimelineFlag;
using Storage::Cache::Error;

class EditHistoryLabel final : public Ui::FlatLabel {
public:
	EditHistoryLabel(QWidget *parent, bool copyRestricted);

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

private:
	const bool _copyRestricted = false;

};

EditHistoryLabel::EditHistoryLabel(QWidget *parent, bool copyRestricted)
: FlatLabel(parent, st::aboutLabel)
, _copyRestricted(copyRestricted) {
}

void EditHistoryLabel::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (pressed && _copyRestricted && !handler->dragText().isEmpty()) {
		ClickHandler::unpressed();
	} else {
		FlatLabel::clickHandlerPressedChanged(handler, pressed);
	}
}

[[nodiscard]] bool HasEditHistory(
		const MessageArchive::TimelineReadResult &result) {
	return result.error.type == Error::Type::None
		&& result.parseError == MessageArchiveStorage::ParseError::None
		&& result.value
		&& result.value->versions.size() > 1;
}

[[nodiscard]] bool MatchesCurrent(
		Main::Session &session,
		FullMsgId id,
		const MessageTimeline &timeline) {
	const auto item = session.data().message(id);
	const auto snapshot = item
		? MessageArchiveStorage::MakeMessageSnapshot(item)
		: std::nullopt;
	return snapshot && MessageArchiveStorage::SameVisibleContent(
		*snapshot,
		timeline.versions.back());
}

[[nodiscard]] rpl::producer<QString> VersionTitle(
		int index,
		int count,
		bool deleted,
		bool current) {
	if (index + 1 == count) {
		return deleted
			? tr::lng_myowngram_edit_history_deleted()
			: current
			? tr::lng_myowngram_edit_history_current()
			: tr::lng_myowngram_edit_history_latest();
	}
	return !index
		? tr::lng_myowngram_edit_history_first()
		: tr::lng_myowngram_edit_history_edited();
}

void ShowVersions(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		FullMsgId id,
		MessageTimeline timeline,
		bool copyRestricted) {
	Expects(timeline.versions.size() > 1);

	struct State {
		MessageTimeline timeline;
		rpl::variable<int> index = 0;
	};
	const auto count = int(timeline.versions.size());
	const auto deleted = bool(timeline.flags & MessageTimelineFlag::Deleted);
	const auto current = !deleted && MatchesCurrent(*session, id, timeline);
	const auto state = box->lifetime().make_state<State>();
	state->timeline = std::move(timeline);
	state->index = count - 1;

	auto title = state->index.value() | rpl::map([=](int index) {
		return VersionTitle(index, count, deleted, current);
	}) | rpl::flatten_latest();
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		std::move(title),
		st::aboutLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_edit_history_position(
			lt_current,
			state->index.value() | rpl::map([](int index) {
				return QString::number(index + 1);
			}),
			lt_total,
			rpl::single(QString::number(count))),
		st::aboutLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->index.value() | rpl::map([=](int index) {
			return state->timeline.versions.empty()
				? QString()
				: Ui::FormatDateTime(QDateTime::fromSecsSinceEpoch(
					state->timeline.versions[index].versionDate));
		}),
		st::aboutLabel));

	const auto body = box->addRow(object_ptr<EditHistoryLabel>(
		box,
		copyRestricted));
	body->setSelectable(!copyRestricted);
	if (copyRestricted) {
		body->setContextMenuHook([](auto &&) {});
	}
	body->setClickHandlerFilter([=](const auto &link, auto) {
		return dynamic_cast<Ui::Text::SpoilerClickHandler*>(link.get())
			|| dynamic_cast<Ui::Text::BlockquoteClickHandler*>(link.get())
			|| (!copyRestricted
				&& dynamic_cast<Ui::Text::PreClickHandler*>(link.get()));
	});
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_edit_history_about(),
		st::aboutLabel));

	const auto previous = box->addLeftButton(
		tr::lng_myowngram_edit_history_previous(),
		[=] {
			if (!state->timeline.versions.empty()
				&& state->index.current() > 0) {
				state->index = state->index.current() - 1;
			}
		});
	const auto next = box->addButton(tr::lng_myowngram_edit_history_next(), [=] {
		if (!state->timeline.versions.empty()
			&& state->index.current() + 1 < count) {
			state->index = state->index.current() + 1;
		}
	});
	const auto weak = base::make_weak(session.get());
	rpl::combine(
		state->index.value(),
		tr::lng_myowngram_edit_history_empty_text()
	) | rpl::on_next([=](int index, const QString &emptyText) {
		const auto strong = weak.get();
		if (!strong || state->timeline.versions.empty()) {
			return;
		}
		const auto &version = state->timeline.versions[index];
		body->setMarkedText(
			version.text.empty() ? tr::marked(emptyText) : version.text,
			Core::TextContext({
				.session = strong,
				.repaint = [=] { body->update(); },
			}));
		previous->setDisabled(index == 0);
		next->setDisabled(index + 1 == count);
		box->scrollToY(0);
	}, box->lifetime());
	session->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
		state->timeline.versions.clear();
		body->setMarkedText(tr::marked());
	}, box->lifetime());
}

void EditHistoryBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		FullMsgId id,
		bool copyRestricted) {
	box->setTitle(tr::lng_myowngram_edit_history_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	session->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_loading(),
		st::aboutLabel));
	session->data().messageArchive().readTimeline(id, crl::guard(box, [=](
			MessageArchive::TimelineReadResult result) {
		status->deleteLater();
		if (!HasEditHistory(result)) {
			const auto failed = result.error.type != Error::Type::None
				|| result.parseError != MessageArchiveStorage::ParseError::None;
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				failed
					? tr::lng_myowngram_edit_history_read_error()
					: tr::lng_myowngram_edit_history_unavailable(),
				st::aboutLabel));
			return;
		}
		const auto item = session->data().message(id);
		ShowVersions(
			box,
			session,
			id,
			std::move(*result.value),
			copyRestricted || (item && item->forbidsSaving()));
	}));
}

} // namespace

void MaybeAddEditHistoryAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller,
		bool copyRestricted,
		QPoint position) {
	if (!item->isRegular() || item->out() || item->history()->peer->isSelf()) {
		return;
	}
	const auto id = item->fullId();
	const auto weak = base::make_weak(controller.get());
	controller->session().data().messageArchive().readTimeline(
		id,
		crl::guard(menu, [=](MessageArchive::TimelineReadResult result) {
			if (!HasEditHistory(result)) {
				return;
			}
			menu->addAction(tr::lng_myowngram_view_edit_history(tr::now), [=] {
				if (const auto strong = weak.get()) {
					strong->show(Box(
						EditHistoryBox,
						&strong->session(),
						id,
						copyRestricted));
				}
			}, &st::menuIconEdit);
			menu->prepareGeometryFor(position);
		}));
}

} // namespace MyOwnGram
