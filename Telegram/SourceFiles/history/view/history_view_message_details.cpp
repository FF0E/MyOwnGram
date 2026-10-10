/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_details.h"

#include "api/api_who_reacted.h"
#include "base/unixtime.h"
#include "data/components/scheduled_messages.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/text/format_values.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {
namespace {

struct Detail {
	QString label;
	QString value;
	QString copy;
	std::vector<Detail> children;
};

using Details = std::vector<Detail>;

void Add(
		Details &details,
		QString label,
		QString value,
		QString copy = {}) {
	if (!value.isEmpty()) {
		details.push_back({
			.label = std::move(label),
			.value = std::move(value),
			.copy = std::move(copy),
		});
	}
}

void AddGroup(Details &details, QString label, Details children) {
	if (!children.empty()) {
		details.push_back({
			.label = std::move(label),
			.children = std::move(children),
		});
	}
}

QString DateText(TimeId date) {
	if (date <= 0) {
		return {};
	}
	const auto parsed = base::unixtime::parse(date);
	return parsed.toOffsetFromUtc(parsed.offsetFromUtc()).toString(Qt::ISODate);
}

void AddDate(
		Details &details,
		Details &rawDates,
		const QString &label,
		TimeId date) {
	if (date > 0) {
		Add(details, label, DateText(date));
		Add(
			rawDates,
			tr::lng_myowngram_details_unix(tr::now, lt_name, label),
			QString::number(date));
	}
}

QString PeerNumber(PeerId id) {
	return QString::number(id.value & PeerId::kChatTypeMask);
}

QString PeerType(PeerId id, PeerData *peer) {
	if (peer && peer->isMegagroup()) {
		return tr::lng_myowngram_details_supergroup(tr::now);
	} else if (id.is<UserId>()) {
		const auto user = peer ? peer->asUser() : nullptr;
		return (user && user->isBot())
			? tr::lng_myowngram_details_bot(tr::now)
			: tr::lng_myowngram_details_user(tr::now);
	} else if (id.is<ChatId>()) {
		return tr::lng_myowngram_details_group(tr::now);
	}
	return peer
		? tr::lng_myowngram_details_channel(tr::now)
		: tr::lng_myowngram_details_channel_or_group(tr::now);
}

void AddPeer(
		Details &details,
		not_null<Data::Session*> owner,
		const QString &label,
		PeerId id) {
	if (!id) {
		return;
	}
	const auto peer = owner->peerLoaded(id);
	if (peer) {
		Add(details, label, peer->name());
	}
	Add(
		details,
		tr::lng_myowngram_details_peer_id(tr::now, lt_name, label),
		PeerNumber(id));
	Add(
		details,
		tr::lng_myowngram_details_peer_type(tr::now, lt_name, label),
		PeerType(id, peer));
}

QString ParticipantText(not_null<Data::Session*> owner, PeerId id) {
	const auto peer = owner->peerLoaded(id);
	const auto identity = PeerType(id, peer) + u" "_q + PeerNumber(id);
	return (peer && !peer->name().isEmpty())
		? peer->name() + u" ("_q + identity + u")"_q
		: identity;
}

Details ChatDetails(not_null<HistoryItem*> item) {
	auto result = Details();
	const auto owner = &item->history()->owner();
	AddPeer(
		result,
		owner,
		tr::lng_myowngram_details_chat(tr::now),
		item->history()->peer->id);
	AddPeer(
		result,
		owner,
		tr::lng_myowngram_details_sender(tr::now),
		item->from()->id);
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (via->bot) {
			AddPeer(
				result,
				owner,
				tr::lng_myowngram_details_via_bot(tr::now),
				via->bot->id);
		}
	}
	if (const auto signature = item->Get<HistoryMessageSigned>()) {
		Add(
			result,
			tr::lng_myowngram_details_signature(tr::now),
			signature->author);
		if (signature->viaBusinessBot) {
			AddPeer(
				result,
				owner,
				tr::lng_myowngram_details_business_bot(tr::now),
				signature->viaBusinessBot->id);
		}
	}
	return result;
}

Details ForwardDetails(not_null<HistoryItem*> item, Details &rawDates) {
	auto result = Details();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return result;
	}
	const auto owner = &item->history()->owner();
	const auto original = tr::lng_myowngram_details_original_sender(tr::now);
	if (forwarded->originalSender) {
		AddPeer(result, owner, original, forwarded->originalSender->id);
	} else if (forwarded->originalHiddenSenderInfo) {
		Add(result, original, forwarded->originalHiddenSenderInfo->name);
	}
	AddDate(
		result,
		rawDates,
		tr::lng_myowngram_details_original_time(tr::now),
		forwarded->originalDate);
	if (forwarded->originalId) {
		Add(
			result,
			tr::lng_myowngram_details_original_post_id(tr::now),
			QString::number(forwarded->originalId.bare));
	}
	Add(
		result,
		tr::lng_myowngram_details_signature(tr::now),
		forwarded->originalPostAuthor);
	if (forwarded->savedFromPeer) {
		AddPeer(
			result,
			owner,
			tr::lng_myowngram_details_saved_chat(tr::now),
			forwarded->savedFromPeer->id);
	}
	if (forwarded->savedFromMsgId) {
		Add(
			result,
			tr::lng_myowngram_details_saved_message_id(tr::now),
			QString::number(forwarded->savedFromMsgId.bare));
	}
	AddDate(
		result,
		rawDates,
		tr::lng_myowngram_details_saved_time(tr::now),
		forwarded->savedFromDate);
	const auto saved = tr::lng_myowngram_details_saved_sender(tr::now);
	if (forwarded->savedFromSender) {
		AddPeer(result, owner, saved, forwarded->savedFromSender->id);
	} else if (forwarded->savedFromHiddenSenderInfo) {
		Add(result, saved, forwarded->savedFromHiddenSenderInfo->name);
	}
	if (forwarded->imported) {
		Add(
			result,
			tr::lng_myowngram_details_imported(tr::now),
			tr::lng_box_yes(tr::now));
	}
	return result;
}

Details ReplyDetails(not_null<HistoryItem*> item) {
	auto result = Details();
	const auto owner = &item->history()->owner();
	const auto reply = item->replyToFullId();
	if (reply.msg) {
		Add(
			result,
			tr::lng_myowngram_details_reply_message_id(tr::now),
			QString::number(reply.msg.bare));
		AddPeer(
			result,
			owner,
			tr::lng_myowngram_details_reply_chat(tr::now),
			reply.peer);
	}
	if (const auto root = item->topicRootId()
		; root && item->history()->isForum()) {
		Add(
			result,
			tr::lng_myowngram_details_topic_id(tr::now),
			QString::number(root.bare));
	}
	if (const auto component = item->Get<HistoryMessageReply>()) {
		const auto &fields = component->fields();
		if (fields.storyId) {
			Add(
				result,
				tr::lng_myowngram_details_reply_story_id(tr::now),
				QString::number(fields.storyId));
			AddPeer(
				result,
				owner,
				tr::lng_myowngram_details_reply_chat(tr::now),
				fields.externalPeerId);
		}
	}
	return result;
}

Details CountDetails(not_null<HistoryItem*> item) {
	auto result = Details();
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->views.count >= 0) {
			Add(
				result,
				tr::lng_myowngram_details_views(tr::now),
				QString::number(views->views.count));
		}
		if (views->forwardsCount >= 0) {
			Add(
				result,
				tr::lng_myowngram_details_forwards(tr::now),
				QString::number(views->forwardsCount));
		}
		if (views->replies.count >= 0) {
			Add(
				result,
				views->commentsMegagroupId
				? tr::lng_myowngram_details_comments(tr::now)
				: tr::lng_myowngram_details_replies(tr::now),
				QString::number(views->replies.count));
		}
	}
	if (!item->reactions().empty()) {
		auto total = int64(0);
		for (const auto &reaction : item->reactions()) {
			total += reaction.count;
		}
		Add(
			result,
			item->reactionsAreTags()
			? tr::lng_myowngram_details_tags(tr::now)
			: tr::lng_myowngram_details_reactions(tr::now),
			QString::number(total));
	}
	return result;
}

void AddSize(Details &details, int64 size) {
	if (size > 0) {
		Add(
			details,
			tr::lng_myowngram_details_size(tr::now),
			Ui::FormatSizeText(size));
		Add(
			details,
			tr::lng_myowngram_details_bytes(tr::now),
			QString::number(size));
	}
}

void AddDimensions(Details &details, QSize size) {
	if (!size.isEmpty()) {
		Add(
			details,
			tr::lng_myowngram_details_dimensions(tr::now),
			QString::number(size.width()) + u" × "_q
				+ QString::number(size.height()));
	}
}

Details AttachmentDetails(not_null<DocumentData*> document) {
	auto result = Details();
	Add(
		result,
		tr::lng_myowngram_details_filename(tr::now),
		document->filename());
	Add(
		result,
		tr::lng_myowngram_details_mime(tr::now),
		document->mimeString());
	AddSize(result, document->size);
	AddDimensions(result, document->dimensions);
	if (document->hasDuration()) {
		Add(
			result,
			tr::lng_myowngram_details_duration(tr::now),
			Ui::FormatDurationText(document->duration() / 1000));
		Add(
			result,
			tr::lng_myowngram_details_duration_ms(tr::now),
			QString::number(document->duration()));
	}
	if (document->id) {
		Add(
			result,
			tr::lng_myowngram_details_document_id(tr::now),
			QString::number(document->id));
	}
	if (document->dcId()) {
		Add(
			result,
			tr::lng_myowngram_details_dc_id(tr::now),
			QString::number(document->dcId()));
	}
	if (const auto song = document->song()) {
		Add(result, tr::lng_myowngram_details_title(tr::now), song->title);
		Add(result, tr::lng_myowngram_details_artist(tr::now), song->performer);
	}
	if (const auto video = document->video()) {
		Add(result, tr::lng_myowngram_details_codec(tr::now), video->codec);
	}
	if (const auto sticker = document->sticker()) {
		Add(result, tr::lng_myowngram_details_emoji(tr::now), sticker->alt);
		auto shortName = sticker->set.shortName;
		if (sticker->set.id) {
			Add(
				result,
				tr::lng_myowngram_details_pack_id(tr::now),
				QString::number(sticker->set.id));
			const auto &sets = document->owner().stickers().sets();
			const auto i = sets.find(sticker->set.id);
			if (i != end(sets)) {
				Add(
					result,
					tr::lng_myowngram_details_pack_title(tr::now),
					i->second->title);
				shortName = i->second->shortName;
			}
		}
		Add(result, tr::lng_myowngram_details_pack_name(tr::now), shortName);
	}
	return result;
}

Details AttachmentDetails(not_null<PhotoData*> photo) {
	auto result = Details();
	if (photo->extendedMediaPreview()) {
		Add(
			result,
			tr::lng_myowngram_details_preview_only(tr::now),
			tr::lng_box_yes(tr::now));
		const auto &location = photo->location(Data::PhotoSize::Large);
		AddDimensions(result, QSize(location.width(), location.height()));
		return result;
	}
	if (photo->id) {
		Add(
			result,
			tr::lng_myowngram_details_photo_id(tr::now),
			QString::number(photo->id));
	}
	if (photo->dcId()) {
		Add(
			result,
			tr::lng_myowngram_details_dc_id(tr::now),
			QString::number(photo->dcId()));
	}
	for (const auto size : {
		Data::PhotoSize::Large,
		Data::PhotoSize::Thumbnail,
		Data::PhotoSize::Small,
	}) {
		if (photo->hasExact(size)) {
			Add(
				result,
				tr::lng_myowngram_details_photo_version(tr::now),
				(size == Data::PhotoSize::Large)
				? tr::lng_myowngram_details_photo_large(tr::now)
				: tr::lng_myowngram_details_photo_thumbnail(tr::now));
			const auto &location = photo->location(size);
			AddDimensions(result, QSize(location.width(), location.height()));
			AddSize(result, photo->imageByteSize(size));
			break;
		}
	}
	return result;
}

Details MediaDetails(
		not_null<HistoryItem*> item,
		PhotoData *selectedPhoto,
		DocumentData *selectedDocument) {
	if (selectedDocument) {
		return AttachmentDetails(selectedDocument);
	} else if (selectedPhoto) {
		return AttachmentDetails(selectedPhoto);
	}
	using Attachment = HistoryMessageMediaForInstantView::Item;
	auto attachments = std::vector<Attachment>();
	const auto add = [&](auto data) {
		if (data && ranges::find(attachments, Attachment(data))
			== end(attachments)) {
			attachments.emplace_back(data);
		}
	};
	if (const auto media = item->media()) {
		add(media->document());
		add(media->photo());
		if (const auto invoice = media->invoice()) {
			for (const auto &part : invoice->extendedMedia) {
				add(part->document());
				add(part->photo());
			}
		}
	}
	if (const auto rich = item->Get<HistoryMessageMediaForInstantView>()) {
		for (const auto &attachment : rich->items) {
			std::visit(add, attachment);
		}
	}
	auto result = Details();
	for (const auto &attachment : attachments) {
		auto fields = std::visit([](auto data) {
			return AttachmentDetails(data);
		}, attachment);
		if (attachments.size() == 1) {
			return fields;
		}
		AddGroup(
			result,
			tr::lng_myowngram_details_attachment_number(
				tr::now,
				lt_count,
				QString::number(result.size() + 1)),
			std::move(fields));
	}
	return result;
}

QString ReadLabel(Ui::WhoReadType type) {
	switch (type) {
	case Ui::WhoReadType::Listened:
		return tr::lng_myowngram_details_listened(tr::now);
	case Ui::WhoReadType::Watched:
		return tr::lng_myowngram_details_watched(tr::now);
	default:
		return tr::lng_myowngram_details_read(tr::now);
	}
}

Details ReadDetails(
		not_null<HistoryItem*> item,
		const Api::WhoReadList &read) {
	auto result = Details();
	const auto owner = &item->history()->owner();
	const auto event = ReadLabel(read.type);
	for (const auto &entry : read.list) {
		const auto person = ParticipantText(owner, entry.peer);
		const auto date = DateText(entry.date);
		Add(
			result,
			person,
			date.isEmpty() ? event : date,
			person + u"\n"_q + event
				+ (date.isEmpty() ? QString() : u": "_q + date
					+ u"\n"_q + tr::lng_myowngram_details_unix(
						tr::now,
						lt_name,
						event) + u": "_q + QString::number(entry.date)));
	}
	return result;
}

QString ReactionText(const Data::ReactionId &reaction) {
	if (reaction.paid()) {
		return tr::lng_myowngram_details_paid_reaction(tr::now);
	} else if (const auto id = reaction.custom()) {
		return tr::lng_myowngram_details_custom_emoji(
			tr::now,
			lt_id,
			QString::number(id));
	}
	return reaction.emoji();
}

Details ReactionDetails(
		not_null<HistoryItem*> item,
		const std::vector<Api::CachedMessageReaction> &cached) {
	auto result = Details();
	for (const auto &entry : item->reactions()) {
		Add(result, ReactionText(entry.id), QString::number(entry.count));
	}
	auto participants = base::flat_map<
		std::pair<PeerId, Data::ReactionId>,
		TimeId>();
	for (const auto &entry : cached) {
		participants[{ entry.peer, entry.reaction }] = entry.date;
	}
	for (const auto &[reaction, recent] : item->recentReactions()) {
		for (const auto &entry : recent) {
			participants.emplace(std::make_pair(entry.peer->id, reaction), 0);
		}
	}
	const auto owner = &item->history()->owner();
	for (const auto &[key, date] : participants) {
		const auto person = ParticipantText(owner, key.first);
		const auto reaction = ReactionText(key.second);
		const auto event = item->reactionsAreTags()
			? tr::lng_myowngram_details_tags(tr::now)
			: tr::lng_myowngram_details_reacted(tr::now);
		const auto time = DateText(date);
		Add(
			result,
			person,
			reaction + (time.isEmpty()
			? QString()
			: u" · "_q + time),
			person + u"\n"_q + event + u": "_q + reaction
				+ (time.isEmpty() ? QString() : u"\n"_q + time
					+ u"\n"_q + tr::lng_myowngram_details_unix(
						tr::now,
						lt_name,
						event) + u": "_q + QString::number(date)));
	}
	return result;
}

Details MessageDetails(
		not_null<HistoryItem*> item,
		PhotoData *selectedPhoto,
		DocumentData *selectedDocument) {
	auto result = Details();
	auto more = Details();
	const auto scheduled = item->isScheduled();
	const auto remoteScheduled = scheduled
		&& !item->isSending()
		&& !item->hasFailed();
	const auto messageId = remoteScheduled
		? item->history()->session().scheduledMessages().lookupId(item)
		: item->id;
	const auto localId = !IsServerMsgId(item->id);
	Add(
		result,
		remoteScheduled
		? tr::lng_myowngram_details_scheduled_id(tr::now)
		: localId
		? tr::lng_myowngram_details_local_id(tr::now)
		: tr::lng_myowngram_details_message_id(tr::now),
		QString::number(messageId.bare));
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	const auto dateLabel = scheduled
		? tr::lng_myowngram_details_scheduled_for(tr::now)
		: localId
		? tr::lng_myowngram_details_created(tr::now)
		: (forwarded && forwarded->imported)
		? tr::lng_myowngram_details_recorded_time(tr::now)
		: item->awaitingVideoProcessing()
		? tr::lng_myowngram_details_estimated_time(tr::now)
		: tr::lng_myowngram_details_sent(tr::now);
	if (IsItemScheduledUntilOnline(item)) {
		Add(result, dateLabel, tr::lng_myowngram_details_when_online(tr::now));
	} else {
		AddDate(result, more, dateLabel, item->date());
	}
	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		AddDate(
			result,
			more,
			tr::lng_myowngram_details_edited(tr::now),
			edited->date);
	}
	const auto cached = Api::LookupCachedMessageInteractions(item);
	const auto singleRead = item->history()->peer->isUser()
		&& cached.read.list.size() == 1
		&& cached.read.list.front().peer == item->history()->peer->id
		&& cached.read.list.front().date > 0;
	if (singleRead) {
		AddDate(
			result,
			more,
			ReadLabel(cached.read.type),
			cached.read.list.front().date);
	}
	result.push_back({});
	AddGroup(
		result,
		tr::lng_myowngram_details_chat_sender(tr::now),
		ChatDetails(item));
	AddGroup(
		result,
		tr::lng_myowngram_details_forwarded(tr::now),
		ForwardDetails(item, more));
	AddGroup(
		result,
		tr::lng_myowngram_details_reply_topic(tr::now),
		ReplyDetails(item));
	AddGroup(
		result,
		tr::lng_myowngram_details_counts(tr::now),
		CountDetails(item));
	const auto media = item->media();
	AddGroup(
		result,
		(media && media->webpage() && !item->Has<HistoryMessageRichPageSource>())
		? tr::lng_myowngram_details_link_preview(tr::now)
		: tr::lng_myowngram_details_attachment(tr::now),
		MediaDetails(item, selectedPhoto, selectedDocument));
	if (!singleRead) {
		AddGroup(
			result,
			tr::lng_myowngram_details_read_times(tr::now),
			ReadDetails(item, cached.read));
	}
	AddGroup(
		result,
		item->reactionsAreTags()
		? tr::lng_myowngram_details_tags(tr::now)
		: tr::lng_myowngram_details_reaction_details(tr::now),
		ReactionDetails(item, cached.reactions));
	if (const auto group = item->groupId()) {
		Add(
			more,
			tr::lng_myowngram_details_album_id(tr::now),
			QString::number(group.raw()));
	}
	Add(
		more,
		tr::lng_myowngram_details_pinned(tr::now),
		item->isPinned() ? tr::lng_box_yes(tr::now) : tr::lng_box_no(tr::now));
	Add(
		more,
		tr::lng_myowngram_details_silent(tr::now),
		item->isSilent() ? tr::lng_box_yes(tr::now) : tr::lng_box_no(tr::now));
	if (media && (media->photo() || media->document()) && !media->webpage()) {
		Add(
			more,
			tr::lng_myowngram_details_spoiler(tr::now),
			media->hasSpoiler()
			? tr::lng_box_yes(tr::now)
			: tr::lng_box_no(tr::now));
	}
	AddGroup(
		result,
		tr::lng_myowngram_details_more(tr::now),
		std::move(more));
	return result;
}

const QString &CopyValue(const Detail &detail) {
	return detail.copy.isEmpty() ? detail.value : detail.copy;
}

void AppendCopyLines(
		QStringList &lines,
		const Details &details,
		const QString &prefix = {}) {
	for (const auto &detail : details) {
		if (detail.label.isEmpty()) {
			continue;
		}
		const auto label = prefix.isEmpty()
			? detail.label
			: prefix + u" / "_q + detail.label;
		if (detail.children.empty()) {
			lines.push_back(label + u": "_q + CopyValue(detail));
		} else {
			AppendCopyLines(lines, detail.children, label);
		}
	}
}

void FillMenu(not_null<Ui::PopupMenu*> menu, const Details &details) {
	const auto addAction = Ui::Menu::CreateAddActionCallback(menu);
	for (const auto &detail : details) {
		if (detail.label.isEmpty()) {
			menu->addSeparator();
		} else if (!detail.children.empty()) {
			addAction({
				.text = detail.label,
				.fillSubmenu = [&](not_null<Ui::PopupMenu*> submenu) {
					FillMenu(submenu, detail.children);
				},
				.submenuSt = &st::messageDetailsPopup,
			});
		} else {
			const auto &st = menu->st().menu;
			const auto font = st.itemStyle.font;
			const auto available = st.widthMax - st.itemPadding.left()
				- st.itemPadding.right() - st.itemRightSkip;
			const auto label = font->elided(
				detail.label.simplified(),
				available / 2);
			const auto value = font->elided(
				detail.value.simplified(),
				available - font->width(label));
			auto action = base::make_unique_q<Ui::Menu::Action>(
				menu->menu(),
				st,
				Ui::Menu::CreateAction(
					menu->menu(),
					detail.label + u": "_q + detail.value,
					[copy = CopyValue(detail)] {
						QGuiApplication::clipboard()->setText(copy);
					}),
				nullptr,
				nullptr);
			action->setMarkedText(tr::marked(label), value);
			menu->addAction(std::move(action));
		}
	}
}

} // namespace

void AddMessageDetailsAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		PhotoData *selectedPhoto,
		DocumentData *selectedDocument) {
	if ((!item->isHistoryEntry() && !item->isScheduled())
		|| item->isSponsored()
		|| item->isFakeAboutView()) {
		return;
	}
	const auto details = MessageDetails(item, selectedPhoto, selectedDocument);
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_myowngram_details_title_menu(tr::now),
		.icon = &st::menuIconInfo,
		.fillSubmenu = [&](not_null<Ui::PopupMenu*> submenu) {
			FillMenu(submenu, details);
			auto lines = QStringList();
			AppendCopyLines(lines, details);
			submenu->addSeparator();
			submenu->addAction(
				tr::lng_myowngram_details_copy_all(tr::now),
				[text = lines.join('\n')] {
					QGuiApplication::clipboard()->setText(text);
				});
		},
		.submenuSt = &st::messageDetailsPopup,
	});
}

} // namespace HistoryView
