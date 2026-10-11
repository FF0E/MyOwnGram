/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message_metadata.h"

#include "base/unixtime.h"
#include "core/credits_amount.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_message_reactions.h"
#include "data/data_photo.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "mtproto/details/mtproto_dump_to_text.h"

#include <limits>

namespace HistoryView {
namespace {

using Fields = std::vector<MessageDetail>;

void Field(Fields &fields, QString name, QString value) {
	if (!value.isEmpty()) {
		fields.push_back({ .label = std::move(name), .value = std::move(value) });
	}
}

void AddGroup(Fields &fields, QString name, Fields values) {
	if (!values.empty()) {
		fields.push_back({
			.label = std::move(name),
			.children = std::move(values),
		});
	}
}

template <typename Value>
void Number(Fields &fields, QString name, Value value) {
	if constexpr (std::is_enum_v<Value>) {
		Number(fields, std::move(name), static_cast<std::underlying_type_t<Value>>(value));
	} else if constexpr (std::is_floating_point_v<Value>) {
		Field(fields, std::move(name), QString::number(
			value, 'g', std::numeric_limits<Value>::max_digits10));
	} else {
		Field(fields, std::move(name), QString::number(value));
	}
}

void Boolean(Fields &fields, QString name, bool value) {
	Field(fields, std::move(name), value
		? tr::lng_box_yes(tr::now)
		: tr::lng_box_no(tr::now));
}

void Bytes(Fields &fields, QString name, const QByteArray &value) {
	Field(fields, name + u".hex"_q, QString::fromLatin1(value.toHex()));
}

void Date(Fields &fields, QString name, TimeId value) {
	if (value > 0) {
		const auto parsed = base::unixtime::parse(value);
		Field(fields, std::move(name), parsed.toOffsetFromUtc(
			parsed.offsetFromUtc()).toString(Qt::ISODate)
			+ u" ("_q + QString::number(value) + u")"_q);
	}
}

void Peer(Fields &fields, QString name, PeerId id) {
	if (id) {
		Field(fields, std::move(name), (id.is<UserId>()
			? u"user:"_q
			: id.is<ChatId>() ? u"chat:"_q : u"channel:"_q)
			+ QString::number(id.value & PeerId::kChatTypeMask));
	}
}

void Peer(Fields &fields, QString name, PeerData *peer) {
	if (peer) {
		Peer(fields, name, peer->id);
		Field(fields, name + u".name"_q, peer->name());
		Field(fields, name + u".username"_q, peer->username());
	}
}

void Text(Fields &fields, QString name, const TextWithEntities &text) {
	auto values = Fields();
	Field(values, u"text"_q, text.text);
	auto index = 0;
	for (const auto &entity : text.entities) {
		auto part = Fields();
		Number(part, u"type"_q, entity.type());
		Number(part, u"offset"_q, entity.offset());
		Number(part, u"length"_q, entity.length());
		Field(part, u"data"_q, entity.data());
		AddGroup(values, u"entities["_q + QString::number(index++) + u"]"_q,
			std::move(part));
	}
	AddGroup(fields, std::move(name), std::move(values));
}

void Size(Fields &fields, QString name, QSize size) {
	if (!size.isEmpty()) {
		Field(fields, std::move(name), QString::number(size.width())
			+ u" × "_q + QString::number(size.height()));
	}
}

Fields RichPageFields(const Iv::RichPage &page) {
	auto result = Fields();
	Field(result, u"url"_q, page.url);
	Boolean(result, u"rtl"_q, page.rtl);
	Boolean(result, u"partial"_q, page.part);
	Number(result, u"views"_q, page.views);
	Number(result, u"blockCount"_q, Iv::CountRichPageBlocks(page));
	Text(result, u"text"_q, Iv::FlattenRichPageToSimpleText(page));
	return result;
}

Fields PhotoFields(not_null<PhotoData*> photo) {
	auto result = Fields();
	Number(result, u"id"_q, photo->id);
	Number(result, u"dcId"_q, photo->dcId());
	Size(result, u"dimensions"_q, QSize(photo->width(), photo->height()));
	Boolean(result, u"previewOnly"_q, photo->extendedMediaPreview());
	if (!photo->extendedMediaPreview()) {
		Date(result, u"date"_q, photo->date());
		Field(result, u"input"_q, MTP::details::DumpObject(photo->mtpInput()));
	}
	Field(result, u"localPath"_q, photo->location(false).name());
	Boolean(result, u"hasAttachedStickers"_q, photo->hasAttachedStickers());
	Boolean(result, u"loading"_q, photo->loading());
	Boolean(result, u"uploading"_q, photo->uploading());
	if (photo->loading() || photo->uploading()) {
		Number(result, u"progress"_q, photo->progress());
		Number(result, u"loadOffset"_q, photo->loadOffset());
	}
	Bytes(result, u"inlineThumbnail"_q, photo->inlineThumbnailBytes());
	for (const auto size : { Data::PhotoSize::Small,
			Data::PhotoSize::Thumbnail, Data::PhotoSize::Large }) {
		if (!photo->hasExact(size)) {
			continue;
		}
		auto part = Fields();
		const auto &location = photo->location(size);
		Size(part, u"dimensions"_q,
			QSize(location.width(), location.height()));
		Number(part, u"bytes"_q, photo->imageByteSize(size));
		AddGroup(result, u"sizes["_q + QString::number(Data::PhotoSizeIndex(size))
			+ u"]"_q, std::move(part));
	}
	if (photo->hasVideo()) {
		Number(result, u"videoStartPositionMs"_q, photo->videoStartPosition());
		Number(result, u"videoBytes"_q, photo->videoByteSize(Data::PhotoSize::Large));
		const auto &large = photo->videoLocation(Data::PhotoSize::Large);
		Size(result, u"videoDimensions"_q,
			QSize(large.width(), large.height()));
		if (photo->hasVideoSmall()) {
			const auto &small = photo->videoLocation(Data::PhotoSize::Small);
			Size(result, u"smallVideoDimensions"_q,
				QSize(small.width(), small.height()));
			Number(result, u"smallVideoBytes"_q,
				photo->videoByteSize(Data::PhotoSize::Small));
		}
	}
	if (const auto duration = photo->extendedMediaVideoDuration()) {
		Number(result, u"previewVideoDurationSeconds"_q, *duration);
	}
	return result;
}

Fields DocumentFields(not_null<DocumentData*> document) {
	auto result = Fields();
	Number(result, u"id"_q, document->id);
	Number(result, u"dcId"_q, document->dcId());
	Field(result, u"filename"_q, document->filename());
	Field(result, u"mime"_q, document->mimeString());
	Number(result, u"bytes"_q, document->size);
	Size(result, u"dimensions"_q, document->dimensions);
	if (document->hasDuration()) {
		Number(result, u"durationMs"_q, document->duration());
	}
	if (const auto song = document->song()) {
		Field(result, u"title"_q, song->title);
		Field(result, u"performer"_q, song->performer);
	}
	Date(result, u"date"_q, document->date);
	Field(result, u"input"_q, MTP::details::DumpObject(document->mtpInput()));
	Field(result, u"localPath"_q, document->filepath(false));
	Field(result, u"loadingPath"_q, document->loadingFilePath());
	Number(result, u"type"_q, document->type);
	Number(result, u"status"_q, document->status);
	Boolean(result, u"loadedInMediaCache"_q, document->loadedInMediaCache());
	Boolean(result, u"loading"_q, document->loading());
	Boolean(result, u"uploading"_q, document->uploading());
	Boolean(result, u"supportsStreaming"_q, document->supportsStreaming());
	Boolean(result, u"silentVideo"_q, document->isSilentVideo());
	Boolean(result, u"hasAttachedStickers"_q, document->hasAttachedStickers());
	Boolean(result, u"forbidsFileSave"_q, document->forbidsFileSave());
	if (document->loading() || document->uploading()) {
		Number(result, u"progress"_q, document->progress());
		Number(result, u"loadOffset"_q, document->loadOffset());
	}
	Bytes(result, u"inlineThumbnail"_q, document->inlineThumbnailBytes());
	if (document->hasThumbnail()) {
		Number(result, u"thumbnailBytes"_q, document->thumbnailByteSize());
		const auto &location = document->thumbnailLocation();
		Size(result, u"thumbnailDimensions"_q,
			QSize(location.width(), location.height()));
	}
	if (document->hasVideoThumbnail()) {
		Number(result, u"videoThumbnailBytes"_q, document->videoThumbnailByteSize());
		const auto &location = document->videoThumbnailLocation();
		Size(result, u"videoThumbnailDimensions"_q,
			QSize(location.width(), location.height()));
	}
	if (const auto video = document->video()) {
		Field(result, u"codec"_q, video->codec);
		Number(result, u"startMs"_q, video->startTs);
		Size(result, u"realVideoDimensions"_q, video->realVideoSize);
		auto index = 0;
		for (const auto quality : video->qualities) {
			auto part = Fields();
			Number(part, u"id"_q, quality->id);
			Number(part, u"bytes"_q, quality->size);
			Size(part, u"dimensions"_q, quality->dimensions);
			AddGroup(result, u"qualities["_q + QString::number(index++) + u"]"_q,
				std::move(part));
		}
	}
	if (const auto voice = document->voice() ? document->voice() : document->round()) {
		auto waveform = QStringList();
		for (const auto sample : voice->waveform) {
			waveform.push_back(QString::number(sample));
		}
		Field(result, u"waveform"_q, waveform.join(u", "_q));
		Number(result, u"waveformMaximum"_q, voice->wavemax);
	}
	if (const auto sticker = document->sticker()) {
		Field(result, u"stickerEmoji"_q, sticker->alt);
		Number(result, u"stickerType"_q, sticker->type);
		Number(result, u"stickerSetType"_q, sticker->setType);
		Boolean(result, u"premiumSticker"_q, document->isPremiumSticker());
		Boolean(result, u"premiumEmoji"_q, document->isPremiumEmoji());
		Boolean(result, u"emojiUsesTextColor"_q, document->emojiUsesTextColor());
	}
	return result;
}

Fields UniqueGiftFields(const Data::UniqueGift &gift) {
	auto result = Fields();
	Number(result, u"id"_q, gift.id);
	Number(result, u"initialGiftId"_q, gift.initialGiftId);
	Number(result, u"number"_q, gift.number);
	Field(result, u"slug"_q, gift.slug);
	Field(result, u"title"_q, gift.title);
	Field(result, u"giftAddress"_q, gift.giftAddress);
	Field(result, u"ownerAddress"_q, gift.ownerAddress);
	Field(result, u"ownerName"_q, gift.ownerName);
	Peer(result, u"owner"_q, gift.ownerId);
	Peer(result, u"host"_q, gift.hostId);
	Peer(result, u"releasedBy"_q, gift.releasedBy);
	Peer(result, u"themeUser"_q, gift.themeUser);
	Number(result, u"nanoTonForResale"_q, gift.nanoTonForResale);
	Number(result, u"starsForResale"_q, gift.starsForResale);
	Number(result, u"starsForTransfer"_q, gift.starsForTransfer);
	Number(result, u"starsMinOffer"_q, gift.starsMinOffer);
	Number(result, u"craftChancePermille"_q, gift.craftChancePermille);
	Boolean(result, u"onlyAcceptTon"_q, gift.onlyAcceptTon);
	Boolean(result, u"canBeTheme"_q, gift.canBeTheme);
	Boolean(result, u"crafted"_q, gift.crafted);
	Boolean(result, u"burned"_q, gift.burned);
	Date(result, u"exportAt"_q, gift.exportAt);
	Date(result, u"canTransferAt"_q, gift.canTransferAt);
	Date(result, u"canResellAt"_q, gift.canResellAt);
	Date(result, u"canCraftAt"_q, gift.canCraftAt);
	Field(result, u"model.name"_q, gift.model.name);
	Number(result, u"model.rarityValue"_q, gift.model.rarityValue);
	Number(result, u"model.documentId"_q, gift.model.document->id);
	Field(result, u"pattern.name"_q, gift.pattern.name);
	Number(result, u"pattern.rarityValue"_q, gift.pattern.rarityValue);
	Number(result, u"pattern.documentId"_q, gift.pattern.document->id);
	Field(result, u"backdrop.name"_q, gift.backdrop.name);
	Number(result, u"backdrop.id"_q, gift.backdrop.id);
	Number(result, u"backdrop.rarityValue"_q, gift.backdrop.rarityValue);
	Field(result, u"backdrop.centerColor"_q, gift.backdrop.centerColor.name(QColor::HexArgb));
	Field(result, u"backdrop.edgeColor"_q, gift.backdrop.edgeColor.name(QColor::HexArgb));
	Field(result, u"backdrop.patternColor"_q, gift.backdrop.patternColor.name(QColor::HexArgb));
	Field(result, u"backdrop.textColor"_q, gift.backdrop.textColor.name(QColor::HexArgb));
	Peer(result, u"originalDetails.sender"_q, gift.originalDetails.senderId);
	Peer(result, u"originalDetails.recipient"_q, gift.originalDetails.recipientId);
	Date(result, u"originalDetails.date"_q, gift.originalDetails.date);
	Text(result, u"originalDetails.message"_q, gift.originalDetails.message);
	if (const auto value = gift.value.get()) {
		Field(result, u"value.currency"_q, value->currency);
		Number(result, u"value.valuePrice"_q, value->valuePrice);
		Number(result, u"value.valuePriceUsd"_q, value->valuePriceUsd);
		Number(result, u"value.initialPriceStars.whole"_q, value->initialPriceStars.whole());
		Number(result, u"value.initialPriceStars.nano"_q, value->initialPriceStars.nano());
		Number(result, u"value.initialSalePrice"_q, value->initialSalePrice);
		Date(result, u"value.initialSaleDate"_q, value->initialSaleDate);
		Number(result, u"value.lastSalePrice"_q, value->lastSalePrice);
		Date(result, u"value.lastSaleDate"_q, value->lastSaleDate);
		Number(result, u"value.averagePrice"_q, value->averagePrice);
		Number(result, u"value.minimumPrice"_q, value->minimumPrice);
		Number(result, u"value.forSaleOnTelegram"_q, value->forSaleOnTelegram);
		Number(result, u"value.forSaleOnFragment"_q, value->forSaleOnFragment);
		Field(result, u"value.fragmentUrl"_q, value->fragmentUrl);
		Boolean(result, u"value.lastSaleFragment"_q, value->lastSaleFragment);
	}
	return result;
}

Fields WebPageFields(not_null<WebPageData*> page) {
	auto result = Fields();
	Number(result, u"id"_q, page->id);
	Number(result, u"type"_q, page->type);
	Field(result, u"url"_q, page->url);
	Field(result, u"displayUrl"_q, page->displayUrl);
	Field(result, u"siteName"_q, page->siteName);
	Field(result, u"title"_q, page->title);
	Field(result, u"author"_q, page->author);
	Text(result, u"description"_q, page->description);
	Number(result, u"durationSeconds"_q, page->duration);
	Boolean(result, u"hasLargeMedia"_q, page->hasLargeMedia);
	Boolean(result, u"photoIsVideoCover"_q, page->photoIsVideoCover);
	Boolean(result, u"failed"_q, page->failed);
	Date(result, u"pendingTill"_q, page->pendingTill);
	if (page->storyId) {
		Peer(result, u"story.peer"_q, page->storyId.peer);
		Number(result, u"story.id"_q, page->storyId.story);
	}
	if (page->photo) {
		AddGroup(result, u"photo"_q, PhotoFields(page->photo));
	}
	if (page->document) {
		AddGroup(result, u"document"_q, DocumentFields(page->document));
	}
	if (page->uniqueGift) {
		AddGroup(result, u"uniqueGift"_q, UniqueGiftFields(*page->uniqueGift));
	}
	if (page->composeToneEmojiId) {
		Number(result, u"composeToneEmojiId"_q, page->composeToneEmojiId);
	}
	if (page->stickerSet) {
		Boolean(result, u"stickerSet.isEmoji"_q, page->stickerSet->isEmoji);
		Boolean(result, u"stickerSet.isTextColor"_q, page->stickerSet->isTextColor);
		auto index = 0;
		for (const auto document : page->stickerSet->items) {
			Number(result, u"stickerSet.items["_q + QString::number(index++)
				+ u"]"_q, document->id);
		}
	}
	auto index = 0;
	for (const auto &item : page->collage.items) {
		AddGroup(result, u"collage["_q + QString::number(index++) + u"]"_q,
			v::match(item, [](PhotoData *photo) {
				return PhotoFields(photo);
			}, [](DocumentData *document) {
				return DocumentFields(document);
			}));
	}
	return result;
}

Fields PollMediaFields(const PollMedia &media) {
	auto result = Fields();
	Field(result, u"url"_q, media.url);
	if (media.photo) {
		AddGroup(result, u"photo"_q, PhotoFields(media.photo));
	}
	if (media.document) {
		AddGroup(result, u"document"_q, DocumentFields(media.document));
	}
	if (media.geo) {
		Field(result, u"geo"_q, MTP::details::DumpObject(media.geo->toMTP()));
	}
	if (media.webpage) {
		AddGroup(result, u"webpage"_q, WebPageFields(media.webpage));
	}
	return result;
}

Fields PollAnswerFields(const PollAnswer &answer) {
	auto result = Fields();
	Text(result, u"text"_q, answer.text);
	Bytes(result, u"option"_q, answer.option);
	AddGroup(result, u"media"_q, PollMediaFields(answer.media));
	Peer(result, u"addedBy"_q, answer.addedBy);
	Date(result, u"addedDate"_q, answer.addedDate);
	Number(result, u"votes"_q, answer.votes);
	Boolean(result, u"chosen"_q, answer.chosen);
	Boolean(result, u"correct"_q, answer.correct);
	auto index = 0;
	for (const auto peer : answer.recentVoters) {
		Peer(result, u"recentVoters["_q + QString::number(index++) + u"]"_q, peer);
	}
	return result;
}

Fields PollFields(not_null<PollData*> poll) {
	auto result = Fields();
	Number(result, u"id"_q, poll->id);
	Text(result, u"question"_q, poll->question);
	Text(result, u"solution"_q, poll->solution);
	AddGroup(result, u"attachedMedia"_q, PollMediaFields(poll->attachedMedia));
	AddGroup(result, u"solutionMedia"_q, PollMediaFields(poll->solutionMedia));
	Number(result, u"totalVoters"_q, poll->totalVoters);
	Number(result, u"hash"_q, poll->hash);
	Number(result, u"closePeriodSeconds"_q, poll->closePeriod);
	Date(result, u"closeDate"_q, poll->closeDate);
	Boolean(result, u"closed"_q, poll->closed());
	Boolean(result, u"publicVotes"_q, poll->publicVotes());
	Boolean(result, u"multiChoice"_q, poll->multiChoice());
	Boolean(result, u"quiz"_q, poll->quiz());
	Boolean(result, u"shuffleAnswers"_q, poll->shuffleAnswers());
	Boolean(result, u"revotingDisabled"_q, poll->revotingDisabled());
	Boolean(result, u"openAnswers"_q, poll->openAnswers());
	Boolean(result, u"hideResultsUntilClose"_q, poll->hideResultsUntilClose());
	Boolean(result, u"creator"_q, poll->creator());
	Boolean(result, u"subscribersOnly"_q, poll->subscribersOnly());
	Boolean(result, u"canViewStats"_q, poll->canViewStats());
	Number(result, u"voteRestriction"_q, poll->voteRestriction());
	auto index = 0;
	for (const auto &answer : poll->answers) {
		AddGroup(result, u"answers["_q + QString::number(index++) + u"]"_q,
			PollAnswerFields(answer));
	}
	index = 0;
	for (const auto peer : poll->recentVoters) {
		Peer(result, u"recentVoters["_q + QString::number(index++) + u"]"_q, peer);
	}
	index = 0;
	for (const auto &vote : poll->sendingVotes) {
		Bytes(result, u"sendingVotes["_q + QString::number(index++) + u"]"_q, vote);
	}
	auto countries = QStringList();
	for (const auto &country : poll->countries) {
		countries.push_back(country);
	}
	Field(result, u"countries"_q, countries.join(u", "_q));
	return result;
}

Fields TodoFields(const std::vector<TodoListItem> &items) {
	auto result = Fields();
	for (const auto &item : items) {
		auto part = Fields();
		Number(part, u"id"_q, item.id);
		Text(part, u"text"_q, item.text);
		Peer(part, u"completedBy"_q, item.completedBy);
		Date(part, u"completionDate"_q, item.completionDate);
		AddGroup(result, QString::number(item.id), std::move(part));
	}
	return result;
}

Fields GiftFields(const Data::GiftCode &gift) {
	auto result = Fields();
	Field(result, u"slug"_q, gift.slug);
	Field(result, u"giftTitle"_q, gift.giftTitle);
	Number(result, u"stargiftId"_q, gift.stargiftId);
	Number(result, u"type"_q, gift.type);
	Number(result, u"count"_q, gift.count);
	Number(result, u"giftNum"_q, gift.giftNum);
	Number(result, u"starsConverted"_q, gift.starsConverted);
	Number(result, u"starsToUpgrade"_q, gift.starsToUpgrade);
	Number(result, u"starsUpgradedBySender"_q, gift.starsUpgradedBySender);
	Number(result, u"starsForDetailsRemove"_q, gift.starsForDetailsRemove);
	Number(result, u"starsBid"_q, gift.starsBid);
	Number(result, u"limitedCount"_q, gift.limitedCount);
	Number(result, u"limitedLeft"_q, gift.limitedLeft);
	Number(result, u"channelSavedId"_q, gift.channelSavedId);
	Number(result, u"giveawayMsgId"_q, gift.giveawayMsgId.bare);
	Number(result, u"realGiftMsgId"_q, gift.realGiftMsgId.bare);
	Peer(result, u"messageAuthor"_q, gift.messageAuthor);
	Peer(result, u"stargiftReleasedBy"_q, gift.stargiftReleasedBy);
	Peer(result, u"auctionTo"_q, gift.auctionTo);
	Peer(result, u"channel"_q, gift.channel);
	Peer(result, u"channelFrom"_q, gift.channelFrom);
	Field(result, u"giftPrepayUpgradeHash"_q, gift.giftPrepayUpgradeHash);
	Text(result, u"message"_q, gift.message);
	Boolean(result, u"viaGiveaway"_q, gift.viaGiveaway);
	Boolean(result, u"transferred"_q, gift.transferred);
	Boolean(result, u"upgradeSeparate"_q, gift.upgradeSeparate);
	Boolean(result, u"upgradeGifted"_q, gift.upgradeGifted);
	Boolean(result, u"upgradable"_q, gift.upgradable);
	Boolean(result, u"unclaimed"_q, gift.unclaimed);
	Boolean(result, u"messageFromUniqueAction"_q, gift.messageFromUniqueAction);
	Boolean(result, u"anonymous"_q, gift.anonymous);
	Boolean(result, u"converted"_q, gift.converted);
	Boolean(result, u"upgraded"_q, gift.upgraded);
	Boolean(result, u"refunded"_q, gift.refunded);
	Boolean(result, u"upgrade"_q, gift.upgrade);
	Boolean(result, u"saved"_q, gift.saved);
	Boolean(result, u"craft"_q, gift.craft);
	if (gift.document) {
		AddGroup(result, u"document"_q, DocumentFields(gift.document));
	}
	if (gift.unique) {
		AddGroup(result, u"unique"_q, UniqueGiftFields(*gift.unique));
	}
	return result;
}

Fields MediaFields(not_null<const Data::Media*> media) {
	auto result = Fields();
	if (const auto ttl = media->ttlSeconds()) {
		Number(result, u"ttlSeconds"_q, ttl);
		Boolean(result, u"singleView"_q, media->ttlSecondsSingleView());
	}
	if (media->photo() || media->document()) {
		Boolean(result, u"hasSpoiler"_q, media->hasSpoiler());
	}
	Text(result, u"consumedMessageText"_q, media->consumedMessageText());
	if (const auto page = media->webpage()) {
		AddGroup(result, u"webpage"_q, WebPageFields(page));
	} else {
		if (const auto photo = media->photo()) {
			AddGroup(result, u"photo"_q, PhotoFields(photo));
		}
		if (const auto document = media->document()) {
			AddGroup(result, u"document"_q, DocumentFields(document));
		}
	}
	if (const auto cover = media->videoCover()) {
		AddGroup(result, u"videoCover"_q, PhotoFields(cover));
	}
	if (const auto timestamp = media->videoTimestamp()) {
		Number(result, u"videoTimestampSeconds"_q, timestamp);
	}
	if (const auto poll = media->poll()) {
		AddGroup(result, u"poll"_q, PollFields(poll));
	}
	if (const auto list = media->todolist()) {
		auto part = Fields();
		Peer(part, u"peerId"_q, list->id.peer);
		Number(part, u"messageId"_q, list->id.msg.bare);
		Text(part, u"title"_q, list->title);
		Boolean(part, u"othersCanAppend"_q, list->othersCanAppend());
		Boolean(part, u"othersCanComplete"_q, list->othersCanComplete());
		AddGroup(part, u"items"_q, TodoFields(list->items));
		AddGroup(result, u"todolist"_q, std::move(part));
	}
	if (const auto contact = media->sharedContact()) {
		auto part = Fields();
		Peer(part, u"userId"_q, peerFromUser(contact->userId));
		Field(part, u"firstName"_q, contact->firstName);
		Field(part, u"lastName"_q, contact->lastName);
		Field(part, u"phoneNumber"_q, contact->phoneNumber);
		for (const auto &[type, value] : contact->vcardItems) {
			Field(part, u"vcard["_q + QString::number(int(type)) + u"]"_q, value);
		}
		AddGroup(result, u"contact"_q, std::move(part));
	}
	if (const auto location = dynamic_cast<const Data::MediaLocation*>(media.get())) {
		auto part = Fields();
		Field(part, u"point"_q, MTP::details::DumpObject(location->point().toMTP()));
		Number(part, u"livePeriodSeconds"_q, location->livePeriod());
		Field(part, u"title"_q, location->title());
		Field(part, u"description"_q, location->description());
		AddGroup(result, u"location"_q, std::move(part));
	}
	if (const auto call = media->call()) {
		auto part = Fields();
		Number(part, u"conferenceId"_q, call->conferenceId);
		Number(part, u"durationSeconds"_q, call->duration);
		Number(part, u"state"_q, call->state);
		Boolean(part, u"video"_q, call->video);
		auto index = 0;
		for (const auto peer : call->otherParticipants) {
			Peer(part, u"participants["_q + QString::number(index++) + u"]"_q, peer);
		}
		AddGroup(result, u"call"_q, std::move(part));
	}
	if (const auto game = media->game()) {
		auto part = Fields();
		if (game->photo) {
			AddGroup(part, u"photo"_q, PhotoFields(game->photo));
		}
		if (game->document) {
			AddGroup(part, u"document"_q, DocumentFields(game->document));
		}
		Number(part, u"id"_q, game->id);
		Number(part, u"accessHash"_q, game->accessHash);
		Field(part, u"shortName"_q, game->shortName);
		Field(part, u"title"_q, game->title);
		Field(part, u"description"_q, game->description);
		AddGroup(result, u"game"_q, std::move(part));
	}
	if (const auto invoice = media->invoice()) {
		auto part = Fields();
		Number(part, u"receiptMsgId"_q, invoice->receiptMsgId.bare);
		Number(part, u"amount"_q, invoice->amount);
		Field(part, u"currency"_q, invoice->currency);
		Field(part, u"title"_q, invoice->title);
		Text(part, u"description"_q, invoice->description);
		Boolean(part, u"isPaidMedia"_q, invoice->isPaidMedia);
		Boolean(part, u"isTest"_q, invoice->isTest);
		if (invoice->photo) {
			AddGroup(part, u"photo"_q, PhotoFields(invoice->photo));
		}
		auto index = 0;
		for (const auto &entry : invoice->extendedMedia) {
			AddGroup(part, u"extendedMedia["_q + QString::number(index++) + u"]"_q,
				MediaFields(entry.get()));
		}
		AddGroup(result, u"invoice"_q, std::move(part));
	}
	if (const auto gift = media->gift()) {
		auto part = GiftFields(*gift);
		Peer(part, u"from"_q,
			static_cast<const Data::MediaGiftBox*>(media.get())->from());
		AddGroup(result, u"gift"_q, std::move(part));
	}
	if (const auto giveaway = media->giveawayStart()) {
		auto part = Fields();
		Date(part, u"untilDate"_q, giveaway->untilDate);
		Number(part, u"quantity"_q, giveaway->quantity);
		Number(part, u"months"_q, giveaway->months);
		Number(part, u"credits"_q, giveaway->credits);
		Boolean(part, u"all"_q, giveaway->all);
		Field(part, u"additionalPrize"_q, giveaway->additionalPrize);
		auto countries = QStringList();
		for (const auto &country : giveaway->countries) {
			countries.push_back(country);
		}
		Field(part, u"countries"_q, countries.join(u", "_q));
		auto index = 0;
		for (const auto peer : giveaway->channels) {
			Peer(part, u"channels["_q + QString::number(index++) + u"]"_q, peer);
		}
		AddGroup(result, u"giveaway"_q, std::move(part));
	}
	if (const auto giveaway = media->giveawayResults()) {
		auto part = Fields();
		Peer(part, u"channel"_q, giveaway->channel);
		Date(part, u"untilDate"_q, giveaway->untilDate);
		Number(part, u"launchId"_q, giveaway->launchId.bare);
		Number(part, u"additionalPeersCount"_q, giveaway->additionalPeersCount);
		Number(part, u"winnersCount"_q, giveaway->winnersCount);
		Number(part, u"unclaimedCount"_q, giveaway->unclaimedCount);
		Number(part, u"months"_q, giveaway->months);
		Number(part, u"credits"_q, giveaway->credits);
		Boolean(part, u"refunded"_q, giveaway->refunded);
		Boolean(part, u"all"_q, giveaway->all);
		Field(part, u"additionalPrize"_q, giveaway->additionalPrize);
		auto index = 0;
		for (const auto peer : giveaway->winners) {
			Peer(part, u"winners["_q + QString::number(index++) + u"]"_q, peer);
		}
		AddGroup(result, u"giveawayResults"_q, std::move(part));
	}
	if (const auto dice = dynamic_cast<const Data::MediaDice*>(media.get())) {
		auto part = Fields();
		Field(part, u"emoji"_q, dice->emoji());
		Number(part, u"value"_q, dice->value());
		if (const auto outcome = dice->diceGameOutcome()) {
			Number(part, u"nanoTon"_q, outcome.nanoTon);
			Number(part, u"stakeNanoTon"_q, outcome.stakeNanoTon);
			Bytes(part, u"seed"_q, outcome.seed);
		}
		AddGroup(result, u"dice"_q, std::move(part));
	}
	if (const auto paper = media->paper()) {
		auto part = Fields();
		Number(part, u"id"_q, paper->id());
		Field(part, u"emojiId"_q, paper->emojiId());
		Field(part, u"settings"_q, MTP::details::DumpObject(paper->mtpSettings()));
		Boolean(part, u"forBoth"_q, media->paperForBoth());
		Boolean(part, u"local"_q, paper->isLocal());
		Peer(part, u"ownerId"_q, peerFromUser(paper->ownerId()));
		AddGroup(result, u"wallpaper"_q, std::move(part));
	}
	if (const auto story = media->storyId()) {
		Peer(result, u"story.peer"_q, story.peer);
		Number(result, u"story.id"_q, story.story);
		Boolean(result, u"story.mention"_q, media->storyMention());
		Boolean(result, u"story.unsupported"_q, media->storyUnsupported());
	}
	return result;
}

Fields ReplyFieldsFor(not_null<HistoryItem*> item) {
	auto result = Fields();
	if (const auto reply = item->Get<HistoryMessageReply>()) {
		const auto &fields = reply->fields();
		Text(result, u"quote"_q, fields.quote);
		if (!fields.quote.empty()) {
			Number(result, u"quoteOffset"_q, fields.quoteOffset);
			Boolean(result, u"manualQuote"_q, fields.manualQuote);
		}
		Peer(result, u"externalSenderId"_q, fields.externalSenderId);
		Peer(result, u"monoforumPeerId"_q, fields.monoforumPeerId);
		Field(result, u"externalSenderName"_q, fields.externalSenderName);
		Field(result, u"externalPostAuthor"_q, fields.externalPostAuthor);
		if (fields.topMessageId) {
			Number(result, u"topMessageId"_q, fields.topMessageId.bare);
		}
		if (fields.todoItemId) {
			Number(result, u"todoItemId"_q, fields.todoItemId);
		}
		Bytes(result, u"pollOption"_q, fields.pollOption);
		Boolean(result, u"topicPost"_q, fields.topicPost);
		Boolean(result, u"unavailable"_q, reply->unavailable());
		if (fields.externalMedia) {
			AddGroup(result, u"externalMedia"_q, MediaFields(fields.externalMedia.get()));
		}
	}
	if (const auto views = item->Get<HistoryMessageViews>()) {
		if (views->commentsMegagroupId) {
			Peer(result, u"commentsMegagroupId"_q, peerFromChannel(views->commentsMegagroupId));
			Number(result, u"commentsRootId"_q, views->commentsRootId.bare);
			Number(result, u"commentsInboxReadTillId"_q, views->commentsInboxReadTillId.bare);
			Number(result, u"commentsMaxId"_q, views->commentsMaxId.bare);
		}
		auto index = 0;
		for (const auto peer : views->recentRepliers) {
			Peer(result, u"recentRepliers["_q + QString::number(index++) + u"]"_q, peer);
		}
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		Field(result, u"forward.psaType"_q, forwarded->psaType);
		Boolean(result, u"forward.savedFromOutgoing"_q, forwarded->savedFromOutgoing);
		Boolean(result, u"forward.story"_q, forwarded->story);
	}
	return result;
}

Fields ButtonFields(const HistoryMessageMarkupButton &button) {
	auto result = Fields();
	Field(result, u"text"_q, button.text);
	Field(result, u"forwardText"_q, button.forwardText);
	Number(result, u"type"_q, button.type);
	Number(result, u"buttonId"_q, button.buttonId);
	Number(result, u"iconId"_q, button.visual.iconId);
	Number(result, u"color"_q, button.visual.color);
	Number(result, u"peerTypes"_q, button.peerTypes.value());
	Bytes(result, u"data"_q, button.data);
	const auto text = QString::fromUtf8(button.data);
	if (text.toUtf8() == button.data) {
		Field(result, u"data.text"_q, text);
	}
	return result;
}

Fields MarkupFields(not_null<HistoryItem*> item) {
	auto result = Fields();
	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		Field(result, u"placeholder"_q, markup->data.placeholder);
		Number(result, u"flags"_q, markup->data.flags.value());
		auto row = 0;
		for (const auto &buttons : markup->data.rows) {
			auto column = 0;
			for (const auto &button : buttons) {
				AddGroup(result, u"rows["_q + QString::number(row) + u"]["_q
					+ QString::number(column++) + u"]"_q, ButtonFields(button));
			}
			++row;
		}
	}
	return result;
}

Fields ReactionFields(not_null<HistoryItem*> item) {
	auto result = Fields();
	for (const auto &reaction : item->chosenReactions()) {
		Field(result, u"chosen"_q, MTP::details::DumpObject(
			Data::ReactionToMTP(reaction)));
		if (item->reactionsAreTags()) {
			Field(
				result,
				u"tagTitle"_q,
				item->history()->owner().reactions().myTagTitle(reaction));
		}
	}
	auto index = 0;
	for (const auto &entry : item->topPaidReactionsWithLocal()) {
		auto part = Fields();
		Peer(part, u"peer"_q, entry.peer);
		Number(part, u"count"_q, entry.count);
		Boolean(part, u"top"_q, entry.top);
		Boolean(part, u"my"_q, entry.my);
		AddGroup(result, u"topPaid["_q + QString::number(index++) + u"]"_q,
			std::move(part));
	}
	index = 0;
	for (const auto &[reaction, entries] : item->recentReactions()) {
		for (const auto &entry : entries) {
			auto part = Fields();
			Field(part, u"reaction"_q, MTP::details::DumpObject(Data::ReactionToMTP(reaction)));
			Peer(part, u"peer"_q, entry.peer);
			Boolean(part, u"unread"_q, entry.unread);
			Boolean(part, u"big"_q, entry.big);
			Boolean(part, u"my"_q, entry.my);
			AddGroup(result, u"recent["_q + QString::number(index++) + u"]"_q,
				std::move(part));
		}
	}
	return result;
}

Fields DependencyFields(const HistoryServiceDependentData &data) {
	auto result = Fields();
	Peer(result, u"peerId"_q, data.peerId);
	if (data.msgId) {
		Number(result, u"messageId"_q, data.msgId.bare);
	}
	if (data.topId) {
		Number(result, u"topId"_q, data.topId.bare);
	}
	Boolean(result, u"topicPost"_q, data.topicPost);
	return result;
}

void Price(Fields &fields, CreditsAmount price) {
	Field(fields, u"currency"_q, price.ton() ? u"TON"_q : u"XTR"_q);
	Number(fields, u"whole"_q, price.whole());
	Number(fields, u"nano"_q, price.nano());
}

Fields ServiceFields(not_null<HistoryItem*> item) {
	auto result = Fields();
	if (const auto data = item->Get<HistoryServicePinned>()) {
		AddGroup(result, u"pinned"_q, DependencyFields(*data));
	}
	if (const auto data = item->Get<HistoryServiceClearHistory>()) {
		AddGroup(result, u"clearHistory"_q, DependencyFields(*data));
	}
	if (const auto data = item->Get<HistoryServiceTopicInfo>()) {
		auto part = DependencyFields(*data);
		Field(part, u"title"_q, data->title);
		Number(part, u"iconId"_q, data->iconId);
		Boolean(part, u"closed"_q, data->closed);
		Boolean(part, u"reopened"_q, data->reopened);
		Boolean(part, u"reiconed"_q, data->reiconed);
		Boolean(part, u"renamed"_q, data->renamed);
		Boolean(part, u"hidden"_q, data->hidden);
		Boolean(part, u"unhidden"_q, data->unhidden);
		AddGroup(result, u"topic"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceTodoCompletions>()) {
		auto part = DependencyFields(*data);
		for (const auto id : data->completed) {
			Number(part, u"completed"_q, id);
		}
		for (const auto id : data->incompleted) {
			Number(part, u"incompleted"_q, id);
		}
		AddGroup(result, u"todoCompletions"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceTodoAppendTasks>()) {
		auto part = DependencyFields(*data);
		AddGroup(part, u"tasks"_q, TodoFields(data->list));
		AddGroup(result, u"todoAppendTasks"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServicePollAppendAnswer>()) {
		auto part = DependencyFields(*data);
		AddGroup(part, u"answer"_q, PollAnswerFields(data->answer));
		AddGroup(result, u"pollAppendAnswer"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServicePollDeleteAnswer>()) {
		auto part = DependencyFields(*data);
		AddGroup(part, u"answer"_q, PollAnswerFields(data->answer));
		AddGroup(result, u"pollDeleteAnswer"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceSuggestDecision>()) {
		auto part = DependencyFields(*data);
		Price(part, data->price);
		Date(part, u"date"_q, data->date);
		Field(part, u"rejectComment"_q, data->rejectComment);
		Boolean(part, u"rejected"_q, data->rejected);
		Boolean(part, u"balanceTooLow"_q, data->balanceTooLow);
		AddGroup(result, u"suggestDecision"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceSuggestFinish>()) {
		auto part = DependencyFields(*data);
		Price(part, data->price);
		Number(part, u"refundType"_q, data->refundType);
		AddGroup(result, u"suggestFinish"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceGameScore>()) {
		auto part = DependencyFields(*data);
		Number(part, u"score"_q, data->score);
		AddGroup(result, u"gameScore"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServicePayment>()) {
		auto part = DependencyFields(*data);
		Field(part, u"slug"_q, data->slug);
		Text(part, u"amount"_q, data->amount);
		Boolean(part, u"recurringInit"_q, data->recurringInit);
		Boolean(part, u"recurringUsed"_q, data->recurringUsed);
		Boolean(part, u"isCreditsCurrency"_q, data->isCreditsCurrency);
		AddGroup(result, u"payment"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServicePaymentRefund>()) {
		auto part = Fields();
		Peer(part, u"peer"_q, data->peer);
		Field(part, u"transactionId"_q, data->transactionId);
		Field(part, u"currency"_q, data->currency);
		Number(part, u"amount"_q, data->amount);
		AddGroup(result, u"paymentRefund"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceSameBackground>()) {
		AddGroup(result, u"sameBackground"_q, DependencyFields(*data));
	}
	if (const auto data = item->Get<HistoryServiceGiveawayResults>()) {
		AddGroup(result, u"giveawayResults"_q, DependencyFields(*data));
	}
	if (const auto data = item->Get<HistoryServiceNoForwardsRequest>()) {
		auto part = Fields();
		Date(part, u"expiresAt"_q, data->expiresAt);
		Boolean(part, u"expired"_q, data->expired);
		Boolean(part, u"actionTaken"_q, data->actionTaken);
		AddGroup(result, u"noForwardsRequest"_q, std::move(part));
	}
	if (const auto data = item->Get<HistoryServiceCommunityAdded>()) {
		Peer(result, u"communityAdded"_q, peerFromChannel(data->communityId));
	}
	if (const auto data = item->Get<HistoryServiceJoinedViaCommunity>()) {
		Peer(result, u"joinedViaCommunity"_q, peerFromChannel(data->communityId));
	}
	if (const auto data = item->Get<HistoryServiceOngoingCall>()) {
		Number(result, u"ongoingCallId"_q, data->id);
	}
	if (const auto data = item->Get<HistoryServiceSelfDestruct>()) {
		auto part = Fields();
		Number(part, u"type"_q, data->type);
		v::match(data->timeToLive, [&](TimeId ttl) {
			Number(part, u"ttlSeconds"_q, ttl);
		}, [&](TimeToLiveSingleView) {
			Boolean(part, u"singleView"_q, true);
		});
		v::match(data->destructAt, [&](TimeId date) {
			Date(part, u"destructAt"_q, date);
		}, [&](TimeToLiveSingleView) {
			Boolean(part, u"viewed"_q, true);
		});
		AddGroup(result, u"selfDestruct"_q, std::move(part));
	}
	return result;
}

Fields StateFields(not_null<HistoryItem*> item) {
	auto result = Fields();
	Boolean(result, u"outgoing"_q, item->out());
	Boolean(result, u"sending"_q, item->isSending());
	Boolean(result, u"failed"_q, item->hasFailed());
	Boolean(result, u"service"_q, item->isService());
	Boolean(result, u"post"_q, item->isPost());
	Boolean(result, u"hideEditedBadge"_q, item->hideEditedBadge());
	Boolean(result, u"invertMedia"_q, item->invertMedia());
	Boolean(result, u"forbidsForward"_q, item->forbidsForward());
	Boolean(result, u"forbidsSaving"_q, item->forbidsSaving());
	Boolean(result, u"sensitiveMedia"_q, item->isMediaSensitive());
	Boolean(result, u"mentionsMe"_q, item->mentionsMe());
	Boolean(result, u"unreadMention"_q, item->isUnreadMention());
	Boolean(result, u"unreadMedia"_q, item->isUnreadMedia());
	Boolean(result, u"unreadReaction"_q, item->hasUnreadReaction());
	Boolean(result, u"unreadPollVote"_q, item->hasUnreadPollVote());
	Date(result, u"ttlDestroyAt"_q, item->ttlDestroyAt());
	Date(result, u"mediaDestroyAt"_q, item->mediaDestroyAt());
	if (const auto effect = item->effectId()) {
		Number(result, u"effectId"_q, effect);
		Boolean(result, u"unwatchedEffect"_q, item->hasUnwatchedEffect());
	}
	if (const auto stars = item->starsPaid()) {
		Number(result, u"starsPaid"_q, stars);
	}
	if (const auto boosts = item->boostsApplied()) {
		Number(result, u"boostsApplied"_q, boosts);
	}
	if (const auto period = item->scheduleRepeatPeriod()) {
		Number(result, u"scheduleRepeatPeriod"_q, period);
	}
	if (item->isBusinessShortcut()) {
		Number(result, u"shortcutId"_q, item->shortcutId());
	}
	Peer(result, u"sublistPeerId"_q, item->sublistPeerId());
	if (const auto rank = item->Get<HistoryMessageFromRank>()) {
		Field(result, u"fromRank"_q, rank->rank);
	}
	if (const auto guest = item->Get<HistoryMessageGuestChat>()) {
		Peer(result, u"guestChatVisitor"_q, guest->visitor);
	}
	if (const auto data = item->Get<HistoryMessageSuggestion>()) {
		auto part = Fields();
		Price(part, data->price);
		Date(part, u"date"_q, data->date);
		Boolean(part, u"accepted"_q, data->accepted);
		Boolean(part, u"rejected"_q, data->rejected);
		if (data->gift) {
			AddGroup(part, u"gift"_q, UniqueGiftFields(*data->gift));
		}
		AddGroup(result, u"suggestion"_q, std::move(part));
	}
	if (const auto signedData = item->Get<HistoryMessageSigned>()) {
		Boolean(result, u"isAnonymousRank"_q, signedData->isAnonymousRank);
	}
	if (const auto reasons = item->Get<HistoryMessageRestrictions>()) {
		auto index = 0;
		for (const auto &reason : reasons->reasons) {
			auto part = Fields();
			Field(part, u"reason"_q, reason.reason);
			Field(part, u"text"_q, reason.text);
			AddGroup(result, u"restrictions["_q + QString::number(index++) + u"]"_q,
				std::move(part));
		}
	}
	return result;
}

} // namespace

std::vector<MessageDetail> CollectMessageMetadata(
		not_null<HistoryItem*> item,
		PhotoData *selectedPhoto,
		DocumentData *selectedDocument) {
	auto result = Fields();
	auto content = Fields();
	Text(content, u"originalText"_q, item->originalText());
	if (const auto translation = item->translation()) {
		Text(content, u"translation"_q, translation->text);
		if (translation->richPage) {
			AddGroup(content, u"richTranslation"_q,
				RichPageFields(*translation->richPage));
		}
	}
	const auto rich = item->fullRichPage()
		? item->fullRichPage()
		: item->richPage();
	if (rich) {
		AddGroup(content, u"richPage"_q, RichPageFields(*rich));
	}
	if (const auto factcheck = item->Get<HistoryMessageFactcheck>()) {
		Text(content, u"factcheck"_q, factcheck->data.text);
		Field(content, u"factcheck.country"_q, factcheck->data.country);
		Number(content, u"factcheck.hash"_q, factcheck->data.hash);
		Boolean(content, u"factcheck.needCheck"_q, factcheck->data.needCheck);
	}
	AddGroup(result, tr::lng_myowngram_details_content(tr::now), std::move(content));
	AddGroup(result, tr::lng_myowngram_details_reply_metadata(tr::now), ReplyFieldsFor(item));
	if (item->history()->isForum()) {
		if (const auto topic = item->topic()) {
			auto values = Fields();
			Field(values, u"title"_q, topic->title());
			Number(values, u"iconId"_q, topic->iconId());
			Number(values, u"colorId"_q, topic->colorId());
			Boolean(values, u"closed"_q, topic->closed());
			Boolean(values, u"hidden"_q, topic->hidden());
			Boolean(values, u"my"_q, topic->my());
			Boolean(values, u"pinned"_q, topic->isPinnedDialog(0));
			Peer(values, u"creatorId"_q, topic->creatorId());
			Date(values, u"creationDate"_q, topic->creationDate());
			AddGroup(result, tr::lng_myowngram_details_topic(tr::now), std::move(values));
		}
	}
	AddGroup(result, tr::lng_myowngram_details_buttons(tr::now), MarkupFields(item));
	AddGroup(result, tr::lng_myowngram_details_reaction_metadata(tr::now), ReactionFields(item));
	if (const auto media = item->media()) {
		AddGroup(result, tr::lng_myowngram_details_media_metadata(tr::now), MediaFields(media));
	}
	const auto media = item->media();
	if (selectedPhoto && (!media || selectedPhoto != media->photo())) {
		AddGroup(
			result,
			tr::lng_myowngram_details_selected_photo(tr::now),
			PhotoFields(selectedPhoto));
	}
	if (selectedDocument && (!media || selectedDocument != media->document())) {
		AddGroup(
			result,
			tr::lng_myowngram_details_selected_document(tr::now),
			DocumentFields(selectedDocument));
	}
	if (const auto instant = item->Get<HistoryMessageMediaForInstantView>()) {
		auto index = 0;
		auto values = Fields();
		Field(values, u"url"_q, instant->url);
		for (const auto &caption : instant->captions) {
			Text(values, u"captions["_q + QString::number(index++) + u"]"_q,
				caption);
		}
		AddGroup(result, tr::lng_myowngram_details_rich_media(tr::now),
			std::move(values));
		index = 0;
		for (const auto &entry : instant->items) {
			AddGroup(result, tr::lng_myowngram_details_attachment_number(
				tr::now, lt_index, QString::number(++index)),
				v::match(entry, [](PhotoData *photo) {
					return PhotoFields(photo);
				}, [](DocumentData *document) {
					return DocumentFields(document);
				}));
		}
	}
	AddGroup(result, tr::lng_myowngram_details_service(tr::now), ServiceFields(item));
	AddGroup(result, tr::lng_myowngram_details_state(tr::now), StateFields(item));
	return result;
}

} // namespace HistoryView
