/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_history.h"

#include "base/timer.h"
#include "core/shortcuts.h"
#include "data/components/message_history.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_service_message.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "profile/profile_back_button.h"
#include "settings/sections/settings_message_history.h"
#include "storage/serialize_message_history.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/jump_down_button.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"

#include <crl/crl_on_main.h>

#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

using Kind = Storage::MessageHistoryKind;
using Entry = Storage::MessageHistoryEntry;
using Item = std::shared_ptr<HistoryItem>;
using OwnedItem = Serialize::MessageHistory::Item;

constexpr auto kSearchDelay = crl::time(250);

struct ViewState {
	ListMemento list{ Data::MaxMessagePosition };
	std::vector<Item> rows;
	bool moreBefore = true;
	bool moreAfter = false;
};

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
	ViewState view;
	std::optional<ViewState> browse;
	bool searchShown = false;

};

class Widget final
	: public Window::SectionWidget
	, private WindowListDelegate {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Memento*> memento);
	~Widget() override;
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
	FullMsgId listMessageHistoryId() override;

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;
	void showFinishedHook() override;
	void updateVisible();
	void updateHeader();
	void updateEmpty();
	void showSearch();
	void cancelSearch();
	void toggleSearch(bool shown, anim::type animated = anim::type::normal);
	void applySearch();
	void jumpToLatest();
	void reload();
	void refresh();
	void showMenu();
	[[nodiscard]] Item createItem(const Entry &entry);
	[[nodiscard]] ViewState saveView();
	void restoreView(ViewState state);
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
	object_ptr<Ui::RpWidget> _topBar;
	object_ptr<Profile::BackButton> _back;
	object_ptr<Ui::PlainShadow> _shadow;
	object_ptr<Ui::IconButton> _menuButton;
	object_ptr<Ui::IconButton> _searchButton;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<Ui::InputField> _search;
	object_ptr<Ui::JumpDownButton> _scrollDown;
	rpl::variable<QString> _emptyActionText;
	object_ptr<Ui::RoundButton> _emptyAction;
	Ui::Text::String _emptyText;
	QRect _emptyRect;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::Timer _searchTimer;
	rpl::event_stream<> _changes;
	std::optional<ViewState> _browse;
	QString _query;
	QString _error;
	qint64 _around = 0;
	int _limitBefore = 0;
	int _limitAfter = 0;
	uint64 _queryRevision = 0;
	int _generation = 0;
	bool _loading = false;
	bool _moreBefore = true;
	bool _moreAfter = false;
	bool _searchShown = false;

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
, _rows(memento->view.rows)
, _scroll(this, controller->chatStyle()->value(lifetime(), st::historyScroll))
, _topBar(this)
, _back(_topBar)
, _shadow(this)
, _menuButton(_topBar, st::topBarMenuToggle)
, _searchButton(_topBar, st::topBarSearch)
, _cancelSearch(_topBar, st::historyAdminLogCancelSearch)
, _search(_topBar, st::defaultMultiSelectSearchField, tr::lng_dlg_filter())
, _scrollDown(_scroll, st::historyToDown)
, _emptyAction(_scroll, _emptyActionText.value(), st::defaultLightButton)
, _searchTimer([=] { applySearch(); })
, _browse(memento->browse)
, _query(memento->search)
, _moreBefore(memento->view.moreBefore)
, _moreAfter(memento->view.moreAfter) {
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_back->setWidget(Ui::CreateChild<Ui::UserpicButton>(
		_back.get(),
		_history->peer,
		st::topBarInfoButton));
	Info::Profile::NameValue(
		_history->peer
	) | rpl::on_next([=](QString name) {
		_back->setText(name);
	}, lifetime());
	_back->setClickedCallback([=] { controller->showBackFromStack(); });
	_menuButton->setClickedCallback([=] { showMenu(); });
	_searchButton->setClickedCallback([=] { showSearch(); });
	_cancelSearch->setClickedCallback([=] { cancelSearch(); });
	_searchButton->setVisible(!_message);
	_scrollDown->setClickedCallback([=] { jumpToLatest(); });
	_scrollDown->hide();
	_search->setText(_query);
	_search->hide();
	_cancelSearch->hide(anim::type::instant);
	_emptyAction->hide();
	_emptyAction->setClickedCallback([=] {
		if (!_error.isEmpty()) {
			reload();
		} else if (!_query.isEmpty()) {
			_search->clear();
			applySearch();
		} else {
			controller->showSettings(Settings::MessageHistoryId());
		}
	});
	_search->changes() | rpl::on_next([=] {
		_cancelSearch->setAccessibleName(_search->getLastText().isEmpty()
			? tr::lng_sr_cancel_search(tr::now)
			: tr::lng_sr_clear_search(tr::now));
		if (_search->getLastText().trimmed().isEmpty()) {
			applySearch();
		} else {
			_searchTimer.callOnce(kSearchDelay);
		}
	}, lifetime());
	_search->submits() | rpl::on_next([=] { applySearch(); }, lifetime());
	_search->cancelled() | rpl::on_next([=] { cancelSearch(); }, lifetime());
	tr::lng_myowngram_message_history_title(
	) | rpl::on_next([=] {
		_back->setSubtext(_message
			? tr::lng_myowngram_message_history_message_title(tr::now)
			: tr::lng_myowngram_message_history_chat_title(tr::now));
		_back->setAccessibleName(tr::lng_terms_back(tr::now));
		_searchButton->setAccessibleName(tr::lng_dlg_filter(tr::now));
		_cancelSearch->setAccessibleName(_search->getLastText().isEmpty()
			? tr::lng_sr_cancel_search(tr::now)
			: tr::lng_sr_clear_search(tr::now));
		_menuButton->setAccessibleName(
			tr::lng_myowngram_message_history_menu(tr::now));
		_scrollDown->setAccessibleName(tr::lng_jump_to_bottom(tr::now));
		updateEmpty();
	}, lifetime());
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return !_message
			&& Ui::AppInFocus()
			&& Ui::InFocusChain(this)
			&& !controller->isLayerShown()
			&& isActiveWindow();
	}) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		request->check(Shortcuts::Command::Search, 2) && request->handle([=] {
			showSearch();
			return true;
		});
	}, lifetime());
	_inner->scrollKeyEvents() | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
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
		return !peer
			|| peer == _history->peer->id
			|| (!_message && _history->migrateFrom()
				&& peer == _history->migrateFrom()->peer->id);
	}) | rpl::on_next([=] {
		if (!_loading
			|| _queryRevision != session().messageHistory().queuedRevision()) {
			refresh();
		}
	}, lifetime());
	_inner->restoreState(&memento->view.list);
	toggleSearch(memento->searchShown || !_query.isEmpty(), anim::type::instant);
}

Widget::~Widget() {
	lifetime().destroy();
	_scroll.destroy();
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
	if (_searchShown) {
		cancelSearch();
	} else {
		controller()->showBackFromStack();
	}
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
	if (_message) {
		return;
	}
	showSearch();
	_search->setText(query);
	applySearch();
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
	controller()->openPhoto(photo, { .id = context });
}

void Widget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .id = context });
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

FullMsgId Widget::listMessageHistoryId() {
	return { _history->peer->id, _message };
}

void Widget::doSetInnerFocus() {
	if (_searchShown) {
		_search->setFocus();
	} else {
		_inner->setFocus();
	}
}

void Widget::showFinishedHook() {
	_inner->showFinished();
}

ViewState Widget::saveView() {
	auto result = ViewState{
		.rows = _rows,
		.moreBefore = _moreBefore,
		.moreAfter = _moreAfter,
	};
	_inner->saveState(&result.list);
	return result;
}

void Widget::restoreView(ViewState state) {
	++_generation;
	_error.clear();
	_rows = state.rows;
	for (const auto &item : _rows) {
		item->Get<HistoryMessageArchived>()->searchQuery = _query;
	}
	_moreBefore = state.moreBefore;
	_moreAfter = state.moreAfter;
	_limitBefore = 0;
	_inner->restoreState(&state.list);
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	applySearch();
	auto result = std::make_shared<Memento>(_history->peer->id, _message);
	result->search = _query;
	result->view = saveView();
	result->browse = _browse;
	result->searchShown = _searchShown;
	return result;
}

void Widget::updateHeader() {
	_topBar->setGeometry(0, 0, width(), st::topBarHeight);
	_menuButton->moveToRight(0, 0);
	const auto searchHiddenLeft = width()
		- _menuButton->width()
		- (_message ? 0 : _searchButton->width());
	const auto searchShownLeft = st::topBarArrowPadding.left();
	_searchButton->moveToLeft(
		_searchShown ? searchShownLeft : searchHiddenLeft,
		0);
	_cancelSearch->moveToLeft(searchHiddenLeft, 0);
	_back->resizeToWidth(std::max(0, searchHiddenLeft));
	_back->moveToLeft(0, 0);
	_back->setVisible(!_searchShown);
	const auto fieldLeft = searchShownLeft + _searchButton->width();
	_search->setGeometryToLeft(
		fieldLeft,
		st::historyAdminLogSearchTop,
		std::max(0, searchHiddenLeft - fieldLeft),
		_search->height());
	_search->setVisible(_searchShown);
	_shadow->setGeometry(0, _back->height(), width(), st::lineWidth);
	_shadow->raise();
}

void Widget::showSearch() {
	toggleSearch(true);
	_search->setFocus();
}

void Widget::toggleSearch(bool shown, anim::type animated) {
	if (_searchShown == shown) {
		return;
	}
	_searchShown = shown;
	_searchButton->setDisabled(shown);
	_cancelSearch->toggle(shown, animated);
	if (!shown) {
		_inner->setFocus();
	}
	updateHeader();
}

void Widget::cancelSearch() {
	if (!_search->getLastText().isEmpty()) {
		_search->clear();
		applySearch();
		_search->setFocus();
	} else {
		toggleSearch(false);
	}
}

void Widget::applySearch() {
	_searchTimer.cancel();
	const auto query = _search->getLastText().trimmed();
	if (query == _query) {
		return;
	}
	if (_query.isEmpty() && !query.isEmpty()) {
		_browse = saveView();
	}
	_query = query;
	if (_query.isEmpty() && _browse) {
		auto state = std::move(*_browse);
		_browse.reset();
		restoreView(std::move(state));
	} else {
		reload();
	}
}

void Widget::jumpToLatest() {
	_inner->showAtPosition(Data::MaxMessagePosition, Window::SectionShow());
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateHeader();
	const auto top = _back->height();
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
	p.fillRect(0, 0, width(), _scroll->y(), st::topBarBg);
}

void Widget::updateVisible() {
	_inner->setVisibleTopBottom(
		_scroll->scrollTop(),
		_scroll->scrollTop() + _scroll->height());
	_scrollDown->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - _scrollDown->height() - st::historyToDownPosition.y());
	_scrollDown->setVisible(!_rows.empty()
		&& (_moreAfter
			|| !_inner->loadedAtBottom()
			|| _scroll->scrollTop() + st::historyToDownShownAfter / 4
				< _scroll->scrollTopMax()));
	_scrollDown->raise();
	updateEmpty();
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
	return a->recordId < b->recordId;
}

void Widget::reload() {
	_searchTimer.cancel();
	restoreView(ViewState());
}

void Widget::refresh() {
	_loading = true;
	updateEmpty();
	_inner->update();
	const auto generation = ++_generation;
	const auto migrated = !_message ? _history->migrateFrom() : nullptr;
	session().messageHistory().query({
		.peer = _history->peer->id,
		.migratedPeer = migrated ? migrated->peer->id : PeerId(),
		.message = _message,
		.around = _around,
		.kind = _message ? Kind::Edited : Kind::Deleted,
		.search = _query,
		.limit = _limitBefore,
		.limitAfter = _limitAfter,
	}, crl::guard(this, [=](Storage::MessageHistoryPage page) {
		if (_generation != generation) {
			return;
		}
		_loading = false;
		_error = page.error;
		if (!_error.isEmpty()) {
			controller()->showToast(tr::lng_myowngram_message_history_error(tr::now));
			updateEmpty();
			_inner->update();
			return;
		}
		_moreBefore = page.moreBefore;
		_moreAfter = page.moreAfter;
		auto existing = base::flat_map<qint64, Item>();
		for (const auto &row : _rows) {
			existing.emplace(row->Get<HistoryMessageArchived>()->recordId, row);
		}
		auto rows = std::vector<Item>();
		rows.reserve(page.entries.size());
		for (const auto &entry : page.entries | ranges::views::reverse) {
			const auto i = existing.find(entry.id);
			rows.push_back(i != existing.end() ? i->second : createItem(entry));
		}
		_rows = std::move(rows);
		_changes.fire({});
		updateEmpty();
		_inner->update();
	}));
	_queryRevision = session().messageHistory().queuedRevision();
}

Item Widget::createItem(const Entry &entry) {
	const auto history = session().data().history(entry.message.peer);
	auto item = Serialize::MessageHistory::Read(history, entry.previous, false);
	if (!item) {
		item = OwnedItem(history->makeMessage({
			.id = history->nextNonHistoryEntryId(),
			.flags = MessageFlag::AdminLogEntry,
			.date = TimeId(entry.savedAt),
		}, PreparedServiceText{
			.text = tr::lng_myowngram_message_history_unreadable(tr::now, tr::marked),
		}).get());
	}
	item->markArchived();
	const auto data = item->Get<HistoryMessageArchived>();
	data->original = entry.message;
	data->recordId = entry.id;
	data->sentAt = item->date();
	data->searchQuery = _query;
	data->deleted = (entry.kind == Kind::Deleted);
	item->updateDate(TimeId(entry.savedAt));
	return Item(std::move(item));
}

Data::MessagesSlice Widget::slice(
		Data::MessagePosition around,
		int before,
		int after) {
	const auto count = int(_rows.size());
	auto i = ranges::find(_rows, around, [](const Item &item) {
		return item->position();
	});
	if (i == _rows.end() && _around) {
		i = ranges::lower_bound(_rows, _around, std::less<>(), [](const Item &item) {
			return item->Get<HistoryMessageArchived>()->recordId;
		});
	}
	const auto center = (i == _rows.end()) ? count - 1 : int(i - _rows.begin());
	const auto from = std::max(0, center - before);
	const auto till = std::min(count, center + after + 1);
	auto result = Data::MessagesSlice();
	if (!_moreBefore && !_moreAfter) {
		result.fullCount = count;
	}
	if (!_moreBefore) {
		result.skippedBefore = from;
	}
	if (!_moreAfter) {
		result.skippedAfter = count - till;
	}
	for (auto index = from; index < till; ++index) {
		result.ids.push_back(_rows[index]->fullId());
		result.ownedItems.push_back(_rows[index]);
	}
	if (center >= 0) {
		result.nearestToAround = _rows[center]->fullId();
	}
	return result;
}

rpl::producer<Data::MessagesSlice> Widget::listSource(
		Data::MessagePosition around,
		int before,
		int after) {
	const auto record = (around == Data::MaxMessagePosition)
		? 0
		: around.fullId.msg.bare;
	if (_around != record || _limitBefore != before + 1 || _limitAfter != after) {
		_around = record;
		_limitBefore = before + 1;
		_limitAfter = after;
		refresh();
	}
	return _changes.events_starting_with(
		rpl::empty_value()
	) | rpl::map([=] { return slice(around, before, after); });
}

void Widget::updateEmpty() {
	if (_loading || !_rows.empty()) {
		_emptyAction->hide();
		return;
	}
	const auto text = !_error.isEmpty()
		? tr::lng_myowngram_message_history_error(tr::now)
		: !_query.isEmpty()
		? tr::lng_myowngram_message_history_no_deleted_results(tr::now)
		: _message
		? tr::lng_myowngram_message_history_no_edits(tr::now)
		: tr::lng_myowngram_message_history_no_deleted(tr::now);
	_emptyText.setText(st::serviceTextStyle, text);
	const auto padding = st::historyAdminLogEmptyPadding;
	const auto outerWidth = std::min(width(), st::historyAdminLogEmptyWidth);
	const auto innerWidth = std::max(1, outerWidth - rect::m::sum::h(padding));
	const auto height = _emptyText.countHeight(innerWidth) + rect::m::sum::v(padding);
	_emptyRect = QRect(
		(width() - outerWidth) / 2,
		_scroll->height() / 3,
		outerWidth,
		height);
	_emptyActionText = !_error.isEmpty()
		? tr::lng_bot_download_retry(tr::now)
		: !_query.isEmpty()
		? tr::lng_sr_clear_search(tr::now)
		: tr::lng_menu_settings(tr::now);
	_emptyAction->moveToLeft(
		(width() - _emptyAction->width()) / 2,
		_emptyRect.y() + _emptyRect.height() + padding.bottom());
	_emptyAction->show();
	_emptyAction->raise();
	_inner->update();
}

void Widget::listPaintEmpty(Painter &p, const Ui::ChatPaintContext &context) {
	if (_loading || !_rows.empty()) {
		return;
	}
	const auto rect = _emptyRect.translated(0, _scroll->scrollTop());
	ServiceMessagePainter::PaintBubble(p, context.st, rect);
	p.setPen(context.st->msgServiceFg());
	const auto padding = st::historyAdminLogEmptyPadding;
	_emptyText.draw(p, {
		.position = rect.topLeft() + QPoint(padding.left(), padding.top()),
		.availableWidth = rect.width() - rect::m::sum::h(padding),
		.align = style::al_top,
	});
}

void Widget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	_menu->addAction(tr::lng_myowngram_message_history_settings(tr::now), [=] {
		controller()->showSettings(Settings::MessageHistoryId());
	}, &st::menuIconSettings);
	_menu->addSeparator();
	_menu->addAction(_message
		? tr::lng_myowngram_message_history_remove(tr::now)
		: tr::lng_myowngram_message_history_clear(tr::now), [=] {
		RemoveMessageHistory(controller(), { _history->peer->id, _message });
	}, &st::menuIconDelete);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(Ui::PopupMenu::ConstrainToParentScreen(
		_menu,
		_topBar->mapToGlobal(QPoint(
			_topBar->width()
				+ st::topBarMenuPosition.x()
				+ Ui::BoxShadow::ExtendFor(_menu->st().shadow).right(),
			st::topBarMenuPosition.y()))));
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
	if (!message) {
		peer = controller->session().data().history(peer)->migrateToOrMe()->peer->id;
	}
	controller->showSection(std::make_shared<Memento>(peer, message));
}

void RemoveMessageHistory(
		not_null<Window::SessionController*> controller,
		FullMsgId message,
		qint64 recordId) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = recordId
				? tr::lng_myowngram_message_history_remove_record_sure()
				: message.msg
				? tr::lng_myowngram_message_history_remove_sure()
				: tr::lng_myowngram_message_history_clear_sure(
					lt_chat,
					rpl::single(controller->session().data().peer(message.peer)->name())),
			.confirmed = [=](Fn<void()> close) {
				const auto kind = recordId
					? std::nullopt
					: std::make_optional(message.msg ? Kind::Edited : Kind::Deleted);
				controller->session().messageHistory().clear(
					message.peer, message.msg, kind, recordId);
				close();
			},
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		});
	}));
}

} // namespace HistoryView
