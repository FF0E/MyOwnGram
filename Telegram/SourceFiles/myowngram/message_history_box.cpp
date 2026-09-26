// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_history_box.h"

#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "myowngram/message_archive.h"
#include "myowngram/message_archive_snapshot.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_extended_data.h"
#include "ui/text/text_utilities.h"
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

constexpr auto kBrowserPageSize = 20;
constexpr auto kBrowserPreviewLength = 200;

class MessageHistoryBrowser final {
public:
	MessageHistoryBrowser(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		not_null<PeerData*> peer);

private:
	struct Cursor {
		PeerId peer;
		MsgId before = ServerMaxMsgId;
	};
	struct Row {
		FullMsgId id;
		MessageArchiveStorage::MessageTimelineMetadata metadata;
		TextWithEntities text;
		bool failed = false;
		bool unavailable = false;
		bool media = false;
	};

	void load();
	void pageRead(MessageArchive::TimelinePage result);
	void readNext();
	void recordRead(MessageArchive::TimelineReadResult result);
	void showPage();
	void updateButtons();
	void confirmRemove(FullMsgId id);
	void remove(FullMsgId id);

	const not_null<Ui::GenericBox*> _box;
	const not_null<Main::Session*> _session;
	const PeerId _peer;
	const not_null<Ui::FlatLabel*> _status;
	const not_null<Ui::VerticalLayout*> _rows;
	QPointer<Ui::RoundButton> _older;
	QPointer<Ui::RoundButton> _newer;
	QPointer<Ui::RoundButton> _refresh;
	Cursor _cursor;
	std::optional<Cursor> _next;
	std::vector<Cursor> _previous;
	std::vector<Row> _page;
	PeerId _migrated;
	size_t _index = 0;
	bool _loading = false;
	bool _closed = false;

};

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
	Expects(!timeline.versions.empty());

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
		bool copyRestricted,
		bool savedRecord) {
	box->setTitle(savedRecord
		? tr::lng_myowngram_message_history_saved()
		: tr::lng_myowngram_edit_history_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	session->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_contacts_loading(),
		st::aboutLabel));
	session->data().messageArchive().readTimeline(id, crl::guard(box, [=](
			MessageArchive::TimelineReadResult result) {
		status->deleteLater();
		if (savedRecord
			? (!result.value || result.value->versions.empty())
			: !HasEditHistory(result)) {
			const auto failed = result.error.type != Error::Type::None
				|| result.parseError != MessageArchiveStorage::ParseError::None;
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				failed
					? tr::lng_myowngram_edit_history_read_error()
					: savedRecord
					? tr::lng_myowngram_message_history_unavailable()
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

MessageHistoryBrowser::MessageHistoryBrowser(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	not_null<PeerData*> peer)
: _box(box)
, _session(session)
, _peer(peer->id)
, _status(box->addRow(object_ptr<Ui::FlatLabel>(
	box,
	rpl::single(peer->name()),
	st::aboutLabel)))
, _rows(box->addRow(object_ptr<Ui::VerticalLayout>(box)))
, _cursor{ peer->id } {
	if (const auto channel = peer->asChannel()) {
		if (const auto chat = channel->getMigrateFromChat()) {
			_migrated = chat->id;
		}
	}
	box->setTitle(tr::lng_myowngram_message_history());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_myowngram_message_history_browser_about(),
		st::aboutLabel));
	_newer = box->addLeftButton(
		tr::lng_myowngram_message_history_newer(),
		[=, this] {
			if (!_loading && !_previous.empty()) {
				_cursor = _previous.back();
				_previous.pop_back();
				load();
			}
		});
	_older = box->addButton(
		tr::lng_myowngram_message_history_older(),
		[=, this] {
			if (!_loading && _next) {
				_previous.push_back(_cursor);
				_cursor = *_next;
				load();
			}
		});
	_refresh = box->addButton(
		tr::lng_myowngram_message_history_refresh(),
		[=, this] {
			if (!_loading) {
				_cursor = { _peer };
				_previous.clear();
				load();
			}
		});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=, this] {
		_closed = true;
		_page.clear();
	}, box->lifetime());
	session->data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=, this] {
		_closed = true;
		_page.clear();
		_rows->clear();
		box->closeBox();
	}, box->lifetime());
	load();
}

void MessageHistoryBrowser::load() {
	if (_closed) {
		return;
	}
	_loading = true;
	updateButtons();
	_status->setText(tr::lng_contacts_loading(tr::now));
	_rows->clear();
	_page.clear();
	_index = 0;
	_session->data().messageArchive().readTimelinePage(
		_cursor.peer,
		_cursor.before,
		kBrowserPageSize,
		crl::guard(_box, [=, this](MessageArchive::TimelinePage result) {
			pageRead(std::move(result));
		}));
}

void MessageHistoryBrowser::pageRead(MessageArchive::TimelinePage result) {
	if (_closed) {
		return;
	} else if (result.error.type != Error::Type::None) {
		_loading = false;
		_next = std::nullopt;
		_status->setText(tr::lng_myowngram_edit_history_read_error(tr::now));
		updateButtons();
		return;
	}
	_next = !result.exhausted
		? std::make_optional(Cursor{ _cursor.peer, result.nextCursor })
		: (_migrated && _cursor.peer != _migrated)
		? std::make_optional(Cursor{ _migrated })
		: std::nullopt;
	for (const auto &entry : result.entries) {
		_page.push_back({ .id = entry.id, .metadata = entry.metadata });
	}
	if (_page.empty() && _next) {
		_cursor = *_next;
		load();
		return;
	}
	readNext();
}

void MessageHistoryBrowser::readNext() {
	if (_closed) {
		return;
	} else if (_index == _page.size()) {
		showPage();
		return;
	}
	// ponytail: Only one full timeline is decoded at a time; a page retains
	// at most 20 short excerpts. Metadata and preview reads are separate,
	// so there is still one follow-up read per record. If measured browser
	// latency warrants it, project excerpts on the storage queue instead.
	_session->data().messageArchive().readTimeline(
		_page[_index].id,
		crl::guard(_box, [=, this](MessageArchive::TimelineReadResult result) {
			recordRead(std::move(result));
		}));
}

void MessageHistoryBrowser::recordRead(
		MessageArchive::TimelineReadResult result) {
	if (_closed) {
		return;
	}
	auto &row = _page[_index++];
	row.failed = result.error.type != Error::Type::None
		|| result.parseError != MessageArchiveStorage::ParseError::None;
	row.unavailable = !result.value || result.value->versions.empty();
	if (!row.failed && !row.unavailable) {
		const auto &timeline = *result.value;
		const auto &snapshot = timeline.versions.back();
		row.metadata = { timeline.flags, timeline.origin };
		auto length = int(std::min<qsizetype>(
			snapshot.text.text.size(),
			kBrowserPreviewLength));
		if (length && snapshot.text.text.at(length - 1).isHighSurrogate()) {
			--length;
		}
		row.text = Ui::Text::Filtered(
			Ui::Text::Mid(snapshot.text, 0, length),
			{ EntityType::Spoiler });
		if (length < snapshot.text.text.size()) {
			row.text.append(u"…"_q);
		}
		row.media = !snapshot.media.isEmpty() || !snapshot.replyMarkup.isEmpty();
	}
	readNext();
}

void MessageHistoryBrowser::showPage() {
	_loading = false;
	_status->setText(_page.empty()
		? tr::lng_myowngram_message_history_empty(tr::now)
		: _session->data().peer(_cursor.peer)->name());
	for (const auto &row : ranges::views::reverse(_page)) {
		const auto id = row.id;
		auto state = (row.metadata.flags & MessageTimelineFlag::HistoryOnly)
			? tr::lng_myowngram_message_history_only()
			: (row.metadata.flags & MessageTimelineFlag::Deleted)
			? tr::lng_myowngram_edit_history_deleted()
			: tr::lng_myowngram_edit_history_latest();
		const auto open = _rows->add(object_ptr<Ui::SettingsButton>(
			_rows,
			tr::lng_myowngram_message_history_entry(
				lt_date,
				rpl::single(Ui::FormatDateTime(QDateTime::fromSecsSinceEpoch(
					row.metadata.origin.date))),
				lt_status,
				std::move(state))));
		open->setClickedCallback([=, this] {
			if (!_loading && !_closed) {
				_box->uiShow()->show(Box(
					EditHistoryBox,
					_session,
					id,
					true,
					true));
			}
		});
		const auto body = _rows->add(object_ptr<EditHistoryLabel>(_rows, true));
		body->setContextMenuHook([](auto &&) {});
		body->setClickHandlerFilter([](const auto &link, auto) {
			return dynamic_cast<Ui::Text::SpoilerClickHandler*>(link.get());
		});
		body->setMarkedText(!row.text.empty()
			? row.text
			: tr::marked(row.failed
				? tr::lng_myowngram_edit_history_read_error(tr::now)
				: row.unavailable
				? tr::lng_myowngram_message_history_unavailable(tr::now)
				: tr::lng_myowngram_edit_history_empty_text(tr::now)));
		if (row.media) {
			_rows->add(object_ptr<Ui::FlatLabel>(
				_rows,
				tr::lng_myowngram_message_history_media(),
				st::aboutLabel));
		}
		const auto remove = _rows->add(object_ptr<Ui::SettingsButton>(
			_rows,
			tr::lng_myowngram_message_history_remove()));
		remove->setClickedCallback([=, this] {
			if (!_loading && !_closed) {
				confirmRemove(id);
			}
		});
	}
	updateButtons();
	_box->scrollToY(_rows->height());
}

void MessageHistoryBrowser::updateButtons() {
	_rows->setDisabled(_loading);
	_older->setDisabled(_loading || !_next);
	_newer->setDisabled(_loading || _previous.empty());
	_refresh->setDisabled(_loading);
}

void MessageHistoryBrowser::confirmRemove(FullMsgId id) {
	_box->uiShow()->show(Ui::MakeConfirmBox({
		.text = tr::lng_myowngram_message_history_remove_sure(),
		.confirmed = crl::guard(_box, [=, this](Fn<void()> &&close) {
			close();
			if (!_loading && !_closed) {
				remove(id);
			}
		}),
		.confirmText = tr::lng_myowngram_message_history_remove(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void MessageHistoryBrowser::remove(FullMsgId id) {
	_loading = true;
	updateButtons();
	_status->setText(tr::lng_myowngram_message_history_removing(tr::now));
	_session->data().messageArchive().removeMessage(
		id,
		crl::guard(_box, [=, this](Error error) {
			if (_closed) {
				return;
			} else if (error.type != Error::Type::None) {
				_loading = false;
				_status->setText(
					tr::lng_myowngram_message_history_remove_error(tr::now));
				updateButtons();
			} else {
				load();
			}
		}));
}

} // namespace

void ShowMessageHistory(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (const auto channel = chat->getMigrateToChannel()) {
			peer = channel;
		}
	}
	const auto session = &controller->session();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->lifetime().make_state<MessageHistoryBrowser>(
			box,
			session,
			peer);
	}));
}

void MaybeAddEditHistoryAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller,
		bool copyRestricted,
		QPoint position) {
	if ((!item->isRegular() && !IsArchivedMsgId(item->id))
		|| item->out()
		|| item->history()->peer->isSelf()) {
		return;
	}
	const auto id = FullMsgId(
		item->history()->peer->id,
		IsArchivedMsgId(item->id) ? OriginalMsgId(item->id) : item->id);
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
						copyRestricted,
						false));
				}
			}, &st::menuIconEdit);
			menu->prepareGeometryFor(position);
		}));
}

} // namespace MyOwnGram
