/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_history.h"

#include "base/timer.h"
#include <crl/crl_on_main.h>
#include "data/components/message_history.h"
#include "data/data_media_types.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "profile/profile_back_button.h"
#include "settings/sections/settings_message_history.h"
#include "storage/serialize_message_history.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

using Kind = Storage::MessageHistoryKind;
using Entry = Storage::MessageHistoryEntry;
using Item = Serialize::MessageHistory::Item;

constexpr auto kSearchDelay = crl::time(250);

class Memento final : public Window::SectionMemento {
public:
	Memento(PeerId peer, MsgId message);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	PeerId peer;
	MsgId message;
	QString search;
	std::optional<Kind> filter;

};

class Widget final
	: public Window::SectionWidget
	, private WindowListDelegate {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Memento*> memento);
	Dialogs::RowDescriptor activeChat() const override;
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;
	Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition around,
		int before,
		int after) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(const QString &query, const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	CopyRestrictionType listCopyRestrictionType(HistoryItem *item) override;
	CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	CopyRestrictionType listSelectRestrictionType() override;
	rpl::producer<Data::AllowedReactions> listAllowedReactionsValue() override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(not_null<const Element*> view) override;
	bool listElementHideTopicButton(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(not_null<TranslateTracker*> tracker) override;
	Ui::ElasticScroll *listScrollArea() const override;
	bool listThanosEffectEnabled() const override;

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;
	void updateVisible();
	void reload();
	void loadMore();
	void showMenu();
	void addEntry(const Entry &entry, std::vector<Item> &rows);
	[[nodiscard]] Data::MessagesSlice slice(
		Data::MessagePosition around,
		int before,
		int after);

	const not_null<History*> _history;
	const MsgId _message;
	std::vector<Item> _rows;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::ElasticScroll> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<Profile::BackButton> _back;
	object_ptr<Ui::IconButton> _menuButton;
	object_ptr<Ui::InputField> _search;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::Timer _searchTimer;
	rpl::event_stream<> _changes;
	QString _query;
	QString _error;
	std::optional<Kind> _filter;
	qint64 _before = 0;
	int _generation = 0;
	bool _loading = false;
	bool _allLoaded = false;

};

Memento::Memento(PeerId peer, MsgId message)
: peer(peer)
, message(message) {
}

Widget::Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Memento*> memento)
: SectionWidget(parent, controller, controller->session().data().peer(memento->peer))
, WindowListDelegate(controller)
, _history(controller->session().data().history(memento->peer))
, _message(memento->message)
, _scroll(this, controller->chatStyle()->value(lifetime(), st::historyScroll))
, _back(this)
, _menuButton(this, st::topBarMenuToggle)
, _search(this, st::defaultInputField,
	Ui::InputField::Mode::SingleLine, tr::lng_dlg_filter(), memento->search)
, _searchTimer([=] { reload(); })
, _query(memento->search)
, _filter(memento->filter) {
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_back->setText(tr::lng_myowngram_message_history_title(tr::now));
	_back->setSubtext(_history->peer->name());
	_back->setClickedCallback([=] { controller->showBackFromStack(); });
	_menuButton->setClickedCallback([=] { showMenu(); });
	_search->changes() | rpl::on_next([=] {
		_searchTimer.callOnce(kSearchDelay);
	}, lifetime());
	_scroll->scrolls() | rpl::on_next([=] { updateVisible(); }, lifetime());
	_scroll->geometryChanged() | rpl::on_next([=] { updateVisible(); }, lifetime());
	Window::ChatThemeValueFromPeer(
		controller,
		_history->peer
	) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
		_inner->update();
		update();
	}, lifetime());
	session().messageHistory().changes(
	) | rpl::filter([=](PeerId peer) {
		return !peer || peer == _history->peer->id;
	}) | rpl::on_next([=] { reload(); }, lifetime());
	auto state = ListMemento();
	state.setAroundPosition(Data::MaxMessagePosition);
	_inner->restoreState(&state);
}

Dialogs::RowDescriptor Widget::activeChat() const {
	return { _history, FullMsgId(_history->peer->id, ShowAtUnreadMsgId) };
}

bool Widget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return false;
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context Widget::listContext() {
	return Context::AdminLog;
}

void Widget::listCancelRequest() {
	controller()->showBackFromStack();
}

void Widget::listDeleteRequest() {
}

void Widget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
}

bool Widget::listAllowsMultiSelect() {
	return false;
}

bool Widget::listIsItemGoodForSelection(not_null<HistoryItem*> item) {
	return false;
}

void Widget::listSelectionChanged(SelectedItems &&items) {
}

void Widget::listMarkReadTill(not_null<HistoryItem*> item) {
}

void Widget::listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData Widget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void Widget::listContentRefreshed() {
	updateVisible();
}

void Widget::listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) {
}

bool Widget::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool Widget::listElementShownUnread(not_null<const Element*> view) {
	return false;
}

bool Widget::listIsGoodForAroundPosition(not_null<const Element*> view) {
	return true;
}

void Widget::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
}

void Widget::listSearch(const QString &query, const FullMsgId &context) {
	_search->setText(query);
}

void Widget::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> Widget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType Widget::listCopyRestrictionType(HistoryItem *item) {
	return CopyRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType Widget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyMediaRestrictionTypeFor(_history->peer, item);
}

CopyRestrictionType Widget::listSelectRestrictionType() {
	return CopyRestrictionTypeFor(_history->peer);
}

rpl::producer<Data::AllowedReactions> Widget::listAllowedReactionsValue() {
	return rpl::single(Data::AllowedReactions());
}

void Widget::listShowPremiumToast(not_null<DocumentData*> document) {
}

void Widget::listOpenPhoto(not_null<PhotoData*> photo, FullMsgId context) {
	controller()->openPhoto(photo, { .itemId = context });
}

void Widget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .itemId = context });
}

QString Widget::listElementAuthorRank(not_null<const Element*> view) {
	return {};
}

bool Widget::listElementHideTopicButton(not_null<const Element*> view) {
	return true;
}

History *Widget::listTranslateHistory() {
	return nullptr;
}

void Widget::listAddTranslatedItems(not_null<TranslateTracker*> tracker) {
}

Ui::ElasticScroll *Widget::listScrollArea() const {
	return _scroll;
}

bool Widget::listThanosEffectEnabled() const {
	return false;
}

void Widget::doSetInnerFocus() {
	_inner->setFocus();
}

void Widget::showFinishedHook() {
	_inner->showFinished();
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_shared<Memento>(_history->peer->id, _message);
	result->search = _query;
	result->filter = _filter;
	return result;
}

void Widget::resizeEvent(QResizeEvent *e) {
	_back->resizeToWidth(width() - _menuButton->width());
	_menuButton->moveToRight(0, 0);
	const auto padding = st::boxRowPadding;
	_search->resizeToWidth(width() - padding.left() - padding.right());
	_search->moveToLeft(padding.left(), _back->height() + padding.top());
	const auto top = _search->y() + _search->height() + padding.bottom();
	_scroll->setGeometry(0, top, width(), std::max(0, height() - top));
	_inner->resizeToWidth(width(), _scroll->height());
	updateVisible();
}

void Widget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	}
	SectionWidget::PaintBackground(controller(), _theme.get(), this, e->rect());
	auto p = Painter(this);
	p.fillRect(0, 0, width(), _scroll->y(), st::boxBg);
}

void Widget::updateVisible() {
	_inner->setVisibleTopBottom(
		_scroll->scrollTop(),
		_scroll->scrollTop() + _scroll->height());
}

bool Widget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (top == _scroll->scrollTop()) {
		updateVisible();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

bool Widget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	const auto a = first->Get<HistoryMessageArchived>();
	const auto b = second->Get<HistoryMessageArchived>();
	return std::tie(a->recordId, a->part) < std::tie(b->recordId, b->part);
}

void Widget::reload() {
	++_generation;
	_loading = false;
	_allLoaded = false;
	_before = 0;
	_query = _search->getLastText().trimmed();
	_error.clear();
	auto removed = base::take(_rows);
	_changes.fire({});
	loadMore();
}

void Widget::loadMore() {
	if (_loading || _allLoaded) {
		return;
	}
	_loading = true;
	const auto generation = _generation;
	session().messageHistory().query({
		.peer = _history->peer->id,
		.message = _message,
		.before = _before,
		.kind = _filter,
		.search = _query,
	}, crl::guard(this, [=](Storage::MessageHistoryPage page) {
		if (_generation != generation) {
			return;
		}
		_loading = false;
		_error = page.error;
		_allLoaded = !page.moreBefore || !_error.isEmpty();
		if (!page.entries.empty()) {
			_before = page.entries.back().id;
		}
		auto added = std::vector<Item>();
		for (const auto &entry : page.entries | ranges::views::reverse) {
			addEntry(entry, added);
		}
		_rows.insert(_rows.begin(),
			std::make_move_iterator(added.begin()),
			std::make_move_iterator(added.end()));
		_changes.fire({});
		_inner->update();
	}));
}

void Widget::addEntry(const Entry &entry, std::vector<Item> &rows) {
	const auto prepare = [&](Item item, int part) {
		item->AddComponents(HistoryMessageArchived::Bit());
		const auto data = item->Get<HistoryMessageArchived>();
		data->original = entry.message;
		data->recordId = entry.id;
		data->part = part;
		data->sentAt = item->date();
		data->deleted = (entry.kind == Kind::Deleted);
		item->updateDate(TimeId(entry.savedAt));
		rows.push_back(std::move(item));
	};
	const auto header = [&](QString text, int part) {
		prepare(Item(_history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = MessageFlag::AdminLogEntry,
			.date = TimeId(entry.savedAt),
		}, PreparedServiceText{ .text = { std::move(text) } }).get()), part);
	};
	if (entry.kind == Kind::Deleted) {
		auto previous = Serialize::MessageHistory::Read(_history, entry.previous, false);
		if (previous) {
			header(tr::lng_myowngram_message_history_deleted_event(tr::now), 0);
			prepare(std::move(previous), 1);
		}
	} else {
		auto updated = Serialize::MessageHistory::Read(_history, entry.updated, false);
		auto previous = Serialize::MessageHistory::Read(_history, entry.previous, false);
		if (updated && previous) {
			header(tr::lng_myowngram_message_history_edited_event(tr::now), 0);
			prepare(std::move(updated), 1);
			header(previous->media() && previous->media()->allowsEditCaption()
				? tr::lng_admin_log_original_caption(tr::now)
				: tr::lng_admin_log_original_message(tr::now), 2);
			prepare(std::move(previous), 3);
		}
	}
}

Data::MessagesSlice Widget::slice(
		Data::MessagePosition around,
		int before,
		int after) {
	const auto count = int(_rows.size());
	const auto i = ranges::find(_rows, around, [](const Item &item) {
		return item->position();
	});
	const auto center = (i == _rows.end()) ? count - 1 : int(i - _rows.begin());
	const auto from = std::max(0, center - before);
	const auto till = std::min(count, center + after + 1);
	auto result = Data::MessagesSlice();
	if (_allLoaded) {
		result.fullCount = count;
		result.skippedBefore = from;
	}
	result.skippedAfter = count - till;
	for (auto index = from; index < till; ++index) {
		result.ids.push_back(_rows[index]->fullId());
	}
	if (center >= 0) {
		result.nearestToAround = _rows[center]->fullId();
	}
	if (!from && !_loading && !_allLoaded) {
		crl::on_main(this, [=] { loadMore(); });
	}
	return result;
}

rpl::producer<Data::MessagesSlice> Widget::listSource(
		Data::MessagePosition around,
		int before,
		int after) {
	return _changes.events_starting_with(
		rpl::empty_value()
	) | rpl::map([=] { return slice(around, before, after); });
}

void Widget::listPaintEmpty(Painter &p, const Ui::ChatPaintContext &context) {
	const auto text = !_error.isEmpty()
		? tr::lng_myowngram_message_history_error(tr::now)
		: _loading
		? tr::lng_contacts_loading(tr::now)
		: tr::lng_myowngram_message_history_empty(tr::now);
	p.setFont(st::normalFont);
	p.setPen(st::windowFg);
	p.drawText(QRect(0, _scroll->scrollTop(), width(), _scroll->height()),
		Qt::AlignCenter, text);
}

void Widget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	_menu->addAction(tr::lng_myowngram_message_history_filter(tr::now), [=] {
		controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
			SingleChoiceBox(box, {
				.title = tr::lng_myowngram_message_history_filter(),
				.options = {
					tr::lng_myowngram_message_history_filter_all(tr::now),
					tr::lng_myowngram_message_history_filter_deleted(tr::now),
					tr::lng_myowngram_message_history_filter_edited(tr::now),
				},
				.initialSelection = _filter ? int(*_filter) + 1 : 0,
				.callback = crl::guard(this, [=](int index) {
					_filter = index ? std::make_optional(Kind(index - 1)) : std::nullopt;
					reload();
				}),
			});
		}));
	}, &st::menuIconSearch);
	_menu->addAction(tr::lng_myowngram_message_history_cleanup(tr::now), [=] {
		controller()->show(Box(Settings::MessageHistoryCleanupBox, controller()));
	}, &st::menuIconTTL);
	_menu->addAction(tr::lng_settings_title(tr::now), [=] {
		controller()->show(Box(Settings::MessageHistorySettingsBox, controller()));
	}, &st::menuIconSettings);
	_menu->addSeparator();
	_menu->addAction(tr::lng_myowngram_message_history_clear(tr::now), [=] {
		RemoveMessageHistory(controller(), { _history->peer->id, _message });
	}, &st::menuIconDelete);
	_menu->popup(_menuButton->mapToGlobal(_menuButton->rect().bottomRight()));
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, this);
	result->setGeometry(geometry);
	return result;
}

} // namespace

void ShowMessageHistory(
		not_null<Window::SessionController*> controller,
		PeerId peer,
		MsgId message) {
	controller->showSection(std::make_shared<Memento>(peer, message));
}

void RemoveMessageHistory(
		not_null<Window::SessionController*> controller,
		FullMsgId message) {
	controller->show(Ui::MakeConfirmBox({
		.text = message.msg
			? tr::lng_myowngram_message_history_remove_sure()
			: tr::lng_myowngram_message_history_clear_sure(),
		.confirmed = [=](Fn<void()> close) {
			controller->session().messageHistory().clear(message.peer, message.msg);
			close();
		},
		.confirmText = tr::lng_myowngram_message_history_remove(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

} // namespace HistoryView
