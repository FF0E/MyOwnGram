/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/delete_own_messages_box.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"

#include <QPainter>
#include <array>
#include <cmath>

#include "styles/style_export.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kBatchSize = 100;

enum class MessageType {
	Text,
	Photo,
	Video,
	VideoMessage,
	Voice,
	Audio,
	Sticker,
	Gif,
	File,
	Poll,
	Other,
	Count,
};

constexpr auto kTypeCount = int(MessageType::Count);

[[nodiscard]] MessageType TypeForDocument(not_null<DocumentData*> document) {
	return document->sticker() ? MessageType::Sticker
		: document->isVideoMessage() ? MessageType::VideoMessage
		: document->isVoiceMessage() ? MessageType::Voice
		: document->isAnimation() ? MessageType::Gif
		: document->isVideoFile() ? MessageType::Video
		: document->isAudioFile() ? MessageType::Audio
		: MessageType::File;
}

[[nodiscard]] MessageType TypeForMessage(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media || media->webpage()) {
		return MessageType::Text;
	} else if (media->poll()) {
		return MessageType::Poll;
	} else if (media->photo()) {
		return MessageType::Photo;
	} else if (const auto document = media->document()) {
		return TypeForDocument(document);
	}
	return MessageType::Other;
}

class DeleteOwnMessages final {
public:
	DeleteOwnMessages(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId replyRootId);

private:
	enum class Phase {
		Settings,
		Searching,
		Deleting,
		Finished,
	};

	void setupSettings();
	void editDate(bool first);
	void updateDates();
	void updateTypes();
	void confirm();
	void start();
	void search();
	void searchDone(const MTPmessages_Messages &result);
	[[nodiscard]] bool matchesType(const MTPDmessage &message) const;
	[[nodiscard]] bool matches(not_null<HistoryItem*> item) const;
	void nextBatch();
	void deleteDone(int count);
	void failed(const MTP::Error &error);
	void updateCounts();
	void stop();
	void finish(rpl::producer<QString> text);

	const not_null<Ui::GenericBox*> _box;
	const not_null<PeerData*> _peer;
	const MsgId _replyRootId;
	MTP::Sender _api;
	mtpRequestId _searchRequestId = 0;

	std::array<Ui::Checkbox*, kTypeCount> _checkboxes = {};
	std::array<bool, kTypeCount> _types = {};
	Ui::Checkbox *_allTypes = nullptr;
	Ui::LinkButton *_fromButton = nullptr;
	Ui::LinkButton *_tillButton = nullptr;
	QPointer<Ui::RoundButton> _deleteButton;
	QDate _fromDate;
	QDate _tillDate;
	TimeId _minDate = 0;
	TimeId _maxDate = 0;

	std::vector<MsgId> _ids;
	MsgId _offsetId = 0;
	int _index = 0;
	int _deleted = 0;
	int _skipped = 0;
	Phase _phase = Phase::Settings;
	bool _stopped = false;
	bool _closed = false;
	bool _deleteInFlight = false;

	rpl::variable<QString> _status;
	rpl::variable<QString> _counts;
	Ui::RpWidget *_progress = nullptr;

};

DeleteOwnMessages::DeleteOwnMessages(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId replyRootId)
: _box(box)
, _peer(peer)
, _replyRootId(replyRootId)
, _api(&peer->session().mtp()) {
	_box->setWidth(st::exportPanelSize.width());
	_box->setMaxHeight(st::boxMaxListHeight);
	_box->setTitle(_replyRootId
		? tr::lng_myowngram_delete_own_replies()
		: tr::lng_myowngram_delete_own_messages());
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		_api.request(base::take(_searchRequestId)).cancel();
	}, _box->lifetime());
	setupSettings();
}

void DeleteOwnMessages::setupSettings() {
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			(_replyRootId
				? tr::lng_myowngram_delete_own_replies_about(
					lt_group,
					rpl::single(_peer->name()))
				: tr::lng_myowngram_delete_own_messages_about(
					lt_group,
					rpl::single(_peer->name()))),
			st::boxLabel),
		st::exportSettingPadding);
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_myowngram_delete_own_dates(),
			st::exportHeaderLabel),
		st::exportHeaderPadding);
	_fromButton = _box->addRow(
		object_ptr<Ui::LinkButton>(_box, QString()),
		st::exportSettingPadding);
	_tillButton = _box->addRow(
		object_ptr<Ui::LinkButton>(_box, QString()),
		st::exportSettingPadding);
	_fromButton->setClickedCallback([=] { editDate(true); });
	_tillButton->setClickedCallback([=] { editDate(false); });
	updateDates();

	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_myowngram_delete_own_types(),
			st::exportHeaderLabel),
		st::exportHeaderPadding);
	_allTypes = _box->addRow(
		object_ptr<Ui::Checkbox>(
			_box,
			tr::lng_myowngram_delete_own_all_types(tr::now),
			true,
			st::defaultBoxCheckbox),
		st::exportSettingPadding);
	_allTypes->setAllowTextLines();
	_allTypes->checkedChanges() | rpl::on_next([=](bool checked) {
		for (const auto checkbox : _checkboxes) {
			checkbox->setChecked(
				checked,
				Ui::Checkbox::NotifyAboutChange::DontNotify);
		}
		updateTypes();
	}, _allTypes->lifetime());

	const auto labels = std::array{
		tr::lng_myowngram_delete_own_text(tr::now),
		tr::lng_export_option_photos(tr::now),
		tr::lng_export_option_video_files(tr::now),
		tr::lng_export_option_video_messages(tr::now),
		tr::lng_export_option_voice_messages(tr::now),
		tr::lng_media_type_songs(tr::now),
		tr::lng_export_option_stickers(tr::now),
		tr::lng_export_option_gifs(tr::now),
		tr::lng_export_option_files(tr::now),
		tr::lng_media_type_polls(tr::now),
		tr::lng_myowngram_delete_own_other(tr::now),
	};
	for (auto i = 0; i != kTypeCount; ++i) {
		const auto checkbox = _box->addRow(
			object_ptr<Ui::Checkbox>(
				_box,
				labels[i],
				true,
				st::defaultBoxCheckbox),
			st::exportSubSettingPadding);
		checkbox->setAllowTextLines();
		_checkboxes[i] = checkbox;
		checkbox->checkedChanges() | rpl::on_next([=] {
			updateTypes();
		}, checkbox->lifetime());
	}
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_myowngram_delete_own_types_about(),
			st::exportAboutLabel),
		st::exportSettingPadding);
	_deleteButton = _box->addButton(
		tr::lng_box_delete(),
		[=] { confirm(); },
		st::attentionBoxButton);
	_box->addButton(tr::lng_cancel(), [=] { _box->closeBox(); });
	updateTypes();
}

void DeleteOwnMessages::editDate(bool first) {
	const auto date = first ? _fromDate : _tillDate;
	const auto min = first ? QDate(2013, 8, 1) : _fromDate;
	const auto max = (first && _tillDate.isValid())
		? _tillDate
		: QDate::currentDate();
	const auto selected = date.isValid() ? date : max;
	const auto apply = crl::guard(_box, [=](QDate value) {
		(first ? _fromDate : _tillDate) = value;
		updateDates();
	});
	_box->getDelegate()->show(Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
		.month = selected,
		.highlighted = selected,
		.callback = [=](QDate value, Fn<void()> close) {
			apply(value);
			close();
		},
		.finalize = [=](not_null<Ui::CalendarBox*> calendar) {
			calendar->addLeftButton(tr::lng_export_from_beginning(), [=] {
				apply(QDate());
				calendar->closeBox();
			});
		},
		.st = st::exportCalendarSizes,
		.minDate = min.isValid() ? min : QDate(2013, 8, 1),
		.maxDate = max,
	}));
}

void DeleteOwnMessages::updateDates() {
	_fromButton->setText(tr::lng_myowngram_delete_own_from(
		tr::now,
		lt_date,
		_fromDate.isValid()
			? langDayOfMonthFull(_fromDate)
			: tr::lng_export_beginning(tr::now)));
	_tillButton->setText(tr::lng_myowngram_delete_own_till(
		tr::now,
		lt_date,
		_tillDate.isValid()
			? langDayOfMonthFull(_tillDate)
			: tr::lng_export_end(tr::now)));
}

void DeleteOwnMessages::updateTypes() {
	auto count = 0;
	for (auto i = 0; i != kTypeCount; ++i) {
		_types[i] = _checkboxes[i]->checked();
		count += _types[i] ? 1 : 0;
	}
	_allTypes->setChecked(
		count == kTypeCount,
		Ui::Checkbox::NotifyAboutChange::DontNotify);
	_deleteButton->setDisabled(!count);
}

void DeleteOwnMessages::confirm() {
	_box->uiShow()->showBox(Ui::MakeConfirmBox({
		.text = (_replyRootId
			? tr::lng_myowngram_delete_own_replies_confirm(
				lt_group,
				rpl::single(_peer->name()))
			: tr::lng_myowngram_delete_own_messages_confirm(
				lt_group,
				rpl::single(_peer->name()))),
		.confirmed = crl::guard(_box, [=](Fn<void()> close) {
			close();
			start();
		}),
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void DeleteOwnMessages::start() {
	if (_phase != Phase::Settings) {
		return;
	}
	_phase = Phase::Searching;
	_minDate = _fromDate.isValid()
		? base::unixtime::serialize(_fromDate.startOfDay())
		: 0;
	_maxDate = _tillDate.isValid()
		? base::unixtime::serialize(_tillDate.addDays(1).startOfDay())
		: 0;
	_box->verticalLayout()->clear();
	_box->clearButtons();
	_box->setCloseByOutsideClick(false);
	_box->addButton(tr::lng_export_stop(), [=] { stop(); });
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			_status.value(),
			st::boxLabel),
		st::exportSettingPadding);
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			_counts.value(),
			st::boxLabel),
		st::exportSettingPadding);
	_progress = _box->addRow(
		object_ptr<Ui::FixedHeightWidget>(_box, st::exportProgressWidth),
		st::exportProgressRowPadding);
	_progress->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_progress);
		const auto ratio = _ids.empty()
			? 0.
			: double(_deleted + _skipped) / _ids.size();
		p.fillRect(_progress->rect(), st::exportProgressBg);
		p.fillRect(
			0,
			0,
			int(std::round(_progress->width() * ratio)),
			_progress->height(),
			st::exportProgressFg);
	}, _progress->lifetime());
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_myowngram_delete_own_progress_about(),
			st::exportAboutLabel),
		st::exportSettingPadding);
	search();
}

void DeleteOwnMessages::search() {
	_status = tr::lng_myowngram_delete_own_searching();
	updateCounts();
	using Flag = MTPmessages_Search::Flag;
	_searchRequestId = _api.request(MTPmessages_Search(
		MTP_flags(Flag::f_from_id
			| (_replyRootId ? Flag::f_top_msg_id : Flag(0))),
		_peer->input(),
		MTP_string(),
		MTP_inputPeerSelf(),
		MTPInputPeer(),
		MTPVector<MTPReaction>(),
		MTP_int(_replyRootId),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(_minDate ? _minDate - 1 : 0),
		MTP_int(_maxDate),
		MTP_int(_offsetId),
		MTP_int(0),
		MTP_int(kBatchSize),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		_searchRequestId = 0;
		searchDone(result);
	}).fail([=](const MTP::Error &error) {
		_searchRequestId = 0;
		failed(error);
	}).send();
}

void DeleteOwnMessages::searchDone(const MTPmessages_Messages &result) {
	auto messages = (const QVector<MTPMessage>*)nullptr;
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		_peer->owner().processUsers(data.vusers());
		_peer->owner().processChats(data.vchats());
		messages = &data.vmessages().v;
	});
	if (!messages) {
		finish(tr::lng_myowngram_delete_own_search_failed());
		return;
	}
	auto nextOffset = MsgId();
	for (const auto &message : *messages) {
		const auto id = IdFromMessage(message);
		if (id <= 0 || (_offsetId && id >= _offsetId)) {
			continue;
		}
		if (!nextOffset || id < nextOffset) {
			nextOffset = id;
		}
		if (message.type() != mtpc_message
			|| PeerFromMessage(message) != _peer->id) {
			continue;
		}
		const auto &data = message.c_message();
		if (!data.vfrom_id()
			|| peerFromMTP(*data.vfrom_id())
				!= _peer->session().userPeerId()
			|| !matchesType(data)) {
			continue;
		}
		const auto item = _peer->owner().addNewMessage(
			message,
			MessageFlags(),
			NewMessageType::Existing);
		if (item && matches(item)) {
			_ids.push_back(id);
		}
	}
	updateCounts();
	if (messages->empty() || result.type() == mtpc_messages_messages) {
		_phase = Phase::Deleting;
		nextBatch();
	} else if (!nextOffset) {
		finish(tr::lng_myowngram_delete_own_search_failed());
	} else {
		_offsetId = nextOffset;
		search();
	}
}

bool DeleteOwnMessages::matchesType(const MTPDmessage &message) const {
	const auto media = message.vmedia();
	if (!media) {
		return _types[int(MessageType::Text)];
	}
	switch (media->type()) {
	case mtpc_messageMediaEmpty:
	case mtpc_messageMediaUnsupported:
	case mtpc_messageMediaWebPage:
	case mtpc_messageMediaVideoStream:
		return _types[int(MessageType::Text)];
	case mtpc_messageMediaPhoto:
		return _types[int(MessageType::Photo)];
	case mtpc_messageMediaPoll:
		return _types[int(MessageType::Poll)];
	case mtpc_messageMediaDocument: {
		const auto types = std::array{
			MessageType::Video,
			MessageType::VideoMessage,
			MessageType::Voice,
			MessageType::Audio,
			MessageType::Sticker,
			MessageType::Gif,
			MessageType::File,
		};
		if (!ranges::any_of(types, [&](MessageType type) {
			return _types[int(type)];
		})) {
			return false;
		}
		const auto document = media->c_messageMediaDocument().vdocument();
		return document
			&& document->type() == mtpc_document
			&& _types[int(TypeForDocument(
				_peer->owner().processDocument(*document)))];
	} break;
	default:
		return _types[int(MessageType::Other)];
	}
}

bool DeleteOwnMessages::matches(not_null<HistoryItem*> item) const {
	const auto root = item->replyToTop()
		? item->replyToTop()
		: item->replyToId();
	return item->isRegular()
		&& !item->isService()
		&& item->history()->peer == _peer
		&& item->from()->isSelf()
		&& (!_replyRootId || root == _replyRootId)
		&& (!_minDate || item->date() >= _minDate)
		&& (!_maxDate || item->date() < _maxDate)
		&& _types[int(TypeForMessage(item))];
}

void DeleteOwnMessages::nextBatch() {
	const auto now = base::unixtime::now();
	auto ids = QVector<MTPint>();
	while (_index < int(_ids.size()) && ids.size() < kBatchSize) {
		const auto id = _ids[_index++];
		const auto item = _peer->owner().message(_peer, id);
		if (!item
			|| !matches(item)
			|| !item->canDelete()
			|| (!_peer->isChannel() && !item->canDeleteForEveryone(now))) {
			++_skipped;
		} else {
			ids.push_back(MTP_int(id));
		}
	}
	updateCounts();
	if (ids.empty()) {
		finish(tr::lng_myowngram_delete_own_finished());
		return;
	}
	_status = tr::lng_myowngram_delete_own_deleting();
	_deleteInFlight = true;
	const auto peer = _peer;
	const auto completed = crl::guard(_box, [=] {
		if (!_closed) {
			deleteDone(ids.size());
		}
	});
	// Sent deletions still update the session if the dialog is closed.
	const auto done = [=](const MTPmessages_AffectedMessages &result) {
		peer->session().api().applyAffectedMessages(peer, result);
		if (peer->isChannel()) {
			peer->owner().processMessagesDeleted(peer->id, ids);
		} else {
			peer->owner().processNonChannelMessagesDeleted(ids);
		}
		completed();
	};
	const auto fail = crl::guard(_box, [=](const MTP::Error &error) {
		if (!_closed) {
			_deleteInFlight = false;
			failed(error);
		}
	});
	if (const auto channel = peer->asChannel()) {
		peer->session().api().request(MTPchannels_DeleteMessages(
			channel->inputChannel(),
			MTP_vector<MTPint>(ids)
		)).done(done).fail(fail).send();
	} else {
		peer->session().api().request(MTPmessages_DeleteMessages(
			MTP_flags(MTPmessages_DeleteMessages::Flag::f_revoke),
			MTP_vector<MTPint>(ids)
		)).done(done).fail(fail).send();
	}
}

void DeleteOwnMessages::deleteDone(int count) {
	_deleteInFlight = false;
	_deleted += count;
	if (_stopped) {
		finish(tr::lng_myowngram_delete_own_stopped());
	} else {
		nextBatch();
	}
}

void DeleteOwnMessages::failed(const MTP::Error &error) {
	finish(tr::lng_myowngram_delete_own_failed());
	MTP::ShowErrorFallback(_box->uiShow(), error);
}

void DeleteOwnMessages::updateCounts() {
	_counts = (_phase == Phase::Searching)
		? tr::lng_myowngram_delete_own_found(
			lt_count,
			rpl::single(float64(_ids.size())))
		: tr::lng_myowngram_delete_own_counts(
			lt_total,
			rpl::single(QString::number(_ids.size())),
			lt_count,
			rpl::single(QString::number(_deleted)),
			lt_skip,
			rpl::single(QString::number(_skipped)));
	_progress->update();
}

void DeleteOwnMessages::stop() {
	_stopped = true;
	_api.request(base::take(_searchRequestId)).cancel();
	if (_deleteInFlight) {
		_status = tr::lng_myowngram_delete_own_stopping();
		_box->clearButtons();
		_box->addButton(tr::lng_close(), [=] { _box->closeBox(); });
	} else {
		finish(tr::lng_myowngram_delete_own_stopped());
	}
}

void DeleteOwnMessages::finish(rpl::producer<QString> text) {
	_phase = Phase::Finished;
	_status = std::move(text);
	updateCounts();
	_box->clearButtons();
	_box->addButton(tr::lng_box_done(), [=] { _box->closeBox(); });
}

} // namespace

void DeleteOwnMessagesBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId replyRootId) {
	box->lifetime().make_state<DeleteOwnMessages>(box, peer, replyRootId);
}
