/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_message_history.h"

#include "api/api_media.h"
#include "core/version.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "iv/iv_data.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"

namespace Serialize {
namespace {

constexpr auto kFormat = qint32(3);

enum class MediaKind : qint32 {
	None,
	Photo,
	Document,
	Contact,
	WebPage,
	Poll,
	Todo,
	Location,
	Dice,
	Game,
	Invoice,
	Story,
	GiveawayStart,
	GiveawayResults,
};

void WriteText(QDataStream &stream, const TextWithEntities &text) {
	stream << text.text << qint32(text.entities.size());
	for (const auto &entity : text.entities) {
		stream << qint32(entity.type()) << qint32(entity.offset())
			<< qint32(entity.length()) << entity.data();
	}
}

TextWithEntities ReadText(QDataStream &stream) {
	auto result = TextWithEntities();
	auto count = qint32(0);
	stream >> result.text >> count;
	for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
		auto type = qint32(0);
		auto offset = qint32(0);
		auto length = qint32(0);
		auto data = QString();
		stream >> type >> offset >> length >> data;
		result.entities.emplace_back(EntityType(type), offset, length, data);
	}
	return result;
}

void WritePeer(QDataStream &stream, PeerData *peer) {
	stream << quint64(peer ? SerializePeerId(peer->id) : 0);
	if (peer) {
		auto writer = ByteArrayWriter();
		Serialize::writePeer(writer, peer);
		stream << std::move(writer).result();
	}
}

PeerId ReadPeer(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion) {
	auto id = quint64(0);
	stream >> id;
	if (!id) {
		return {};
	}
	auto bytes = QByteArray();
	stream >> bytes;
	const auto peerId = DeserializePeerId(id);
	if (!session->data().peerLoaded(peerId)) {
		auto reader = ByteArrayReader(std::move(bytes));
		if (!Serialize::readPeer(session, appVersion, reader)) {
			stream.setStatus(QDataStream::ReadCorruptData);
		}
	}
	return peerId;
}

template <typename Type>
void WriteTL(QDataStream &stream, const Type &value) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	stream << QByteArray(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size() * sizeof(mtpPrime));
}

template <typename Type>
Type ReadTL(QDataStream &stream) {
	auto bytes = QByteArray();
	stream >> bytes;
	auto buffer = mtpBuffer(bytes.size() / sizeof(mtpPrime));
	if (bytes.size() % sizeof(mtpPrime)) {
		stream.setStatus(QDataStream::ReadCorruptData);
		return {};
	}
	if (!bytes.isEmpty()) {
		memcpy(buffer.data(), bytes.constData(), bytes.size());
	}
	auto from = static_cast<const mtpPrime*>(buffer.data());
	auto result = Type();
	if (!result.read(from, from + buffer.size())) {
		stream.setStatus(QDataStream::ReadCorruptData);
	}
	return result;
}

void WriteImage(
		QDataStream &stream,
		const ImageLocation &location,
		int size) {
	writeImageLocation(stream, location);
	stream << qint32(size);
}

ImageWithLocation ReadImage(QDataStream &stream, int appVersion) {
	const auto location = readImageLocation(appVersion, stream);
	auto size = qint32(0);
	stream >> size;
	if (!location) {
		stream.setStatus(QDataStream::ReadCorruptData);
		return {};
	}
	return { .location = *location, .bytesCount = size };
}

} // namespace

void MessageHistory::WritePhoto(QDataStream &stream, PhotoData *photo) {
	stream << quint64(photo ? photo->id : 0);
	if (!photo) {
		return;
	}
	stream << qint32(photo->_dc) << quint64(photo->_access)
		<< photo->fileReference() << qint32(photo->date())
		<< photo->hasAttachedStickers() << photo->inlineThumbnailBytes();
	for (const auto size : { Data::PhotoSize::Small,
		Data::PhotoSize::Thumbnail, Data::PhotoSize::Large }) {
		WriteImage(stream, photo->location(size), photo->imageByteSize(size));
	}
	for (const auto size : { Data::PhotoSize::Small, Data::PhotoSize::Large }) {
		WriteImage(stream, photo->videoLocation(size), photo->videoByteSize(size));
	}
	stream << qint64(photo->videoStartPosition());
}

PhotoData *MessageHistory::ReadPhoto(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion) {
	auto id = quint64(0);
	stream >> id;
	if (!id) {
		return nullptr;
	}
	auto dc = qint32(0);
	auto access = quint64(0);
	auto reference = QByteArray();
	auto date = qint32(0);
	auto stickers = false;
	auto thumbnailBytes = QByteArray();
	stream >> dc >> access >> reference >> date >> stickers >> thumbnailBytes;
	const auto small = ReadImage(stream, appVersion);
	const auto thumbnail = ReadImage(stream, appVersion);
	const auto large = ReadImage(stream, appVersion);
	const auto videoSmall = ReadImage(stream, appVersion);
	const auto videoLarge = ReadImage(stream, appVersion);
	auto start = qint64(0);
	stream >> start;
	const auto photo = session->data().photo(id);
	if (photo->isNull() && stream.status() == QDataStream::Ok) {
		photo->setRemoteLocation(dc, access, reference);
		photo->setFields(date, stickers);
		photo->updateImages(
			thumbnailBytes,
			small,
			thumbnail,
			large,
			videoSmall,
			videoLarge,
			start);
	}
	return photo;
}

void MessageHistory::WriteDocument(
		QDataStream &stream,
		DocumentData *document) {
	stream << quint64(document ? document->id : 0);
	if (!document) {
		return;
	}
	stream << qint32(document->_dc) << quint64(document->_access)
		<< document->fileReference() << qint32(document->date)
		<< document->mimeString() << qint64(document->size)
		<< document->inlineThumbnailBytes() << document->inlineThumbnailIsPath()
		<< document->isPremiumSticker();
	WriteTL(stream, Api::ComposeSendingDocumentAttributes(document));
	WriteImage(stream, document->thumbnailLocation(), document->thumbnailByteSize());
	WriteImage(
		stream,
		document->videoThumbnailLocation(),
		document->videoThumbnailByteSize());
}

DocumentData *MessageHistory::ReadDocument(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion) {
	auto id = quint64(0);
	stream >> id;
	if (!id) {
		return nullptr;
	}
	auto dc = qint32(0);
	auto access = quint64(0);
	auto reference = QByteArray();
	auto date = qint32(0);
	auto mime = QString();
	auto size = qint64(0);
	auto inlineBytes = QByteArray();
	auto path = false;
	auto premium = false;
	stream >> dc >> access >> reference >> date >> mime >> size
		>> inlineBytes >> path >> premium;
	const auto attributes = ReadTL<MTPVector<MTPDocumentAttribute>>(stream);
	const auto thumbnail = ReadImage(stream, appVersion);
	const auto videoThumbnail = ReadImage(stream, appVersion);
	const auto existing = session->data().document(id);
	if (!existing->isNull() || stream.status() != QDataStream::Ok) {
		return existing;
	}
	return session->data().document(
		id,
		access,
		reference,
		date,
		attributes.v,
		mime,
		InlineImageLocation{ inlineBytes, path },
		thumbnail,
		videoThumbnail,
		premium,
		dc,
		size);
}

void MessageHistory::WriteRich(
		QDataStream &stream,
		not_null<HistoryItem*> item) {
	const auto page = item->fullRichPage() ? item->fullRichPage() : item->richPage();
	const auto session = &item->history()->session();
	const auto serialized = page
		? Iv::SerializeInputRichMessage(
			session, *page, Iv::SerializeInputRichMessageMode::Draft)
		: Iv::SerializeInputRichMessageResult();
	stream << serialized.value.has_value();
	if (!serialized.value) {
		return;
	}
	stream << page->part;
	WriteTL(stream, *serialized.value);
	const auto &data = serialized.value->c_inputRichMessage();
	const auto photos = data.vphotos();
	stream << qint32(photos ? photos->v.size() : 0);
	if (photos) {
		for (const auto &photo : photos->v) {
			WritePhoto(stream, session->data().photo(photo.c_inputPhoto().vid().v));
		}
	}
	const auto documents = data.vdocuments();
	stream << qint32(documents ? documents->v.size() : 0);
	if (documents) {
		for (const auto &document : documents->v) {
			WriteDocument(stream, session->data().document(
				document.c_inputDocument().vid().v));
		}
	}
	const auto users = data.vusers();
	stream << qint32(users ? users->v.size() : 0);
	if (users) {
		for (const auto &user : users->v) {
			WritePeer(stream, user.type() == mtpc_inputUserSelf
				? session->user()
				: session->data().user(UserId(user.c_inputUser().vuser_id().v)));
		}
	}
}

std::shared_ptr<const Iv::RichPage> MessageHistory::ReadRich(
		QDataStream &stream,
		not_null<Main::Session*> session,
		int appVersion) {
	auto exists = false;
	stream >> exists;
	if (!exists) {
		return nullptr;
	}
	auto part = false;
	stream >> part;
	const auto input = ReadTL<MTPInputRichMessage>(stream);
	auto count = qint32(0);
	stream >> count;
	for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
		ReadPhoto(stream, session, appVersion);
	}
	stream >> count;
	for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
		ReadDocument(stream, session, appVersion);
	}
	stream >> count;
	for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
		ReadPeer(stream, session, appVersion);
	}
	return (stream.status() == QDataStream::Ok)
		? Iv::ParseRichPage(session, input, part)
		: nullptr;
}

void MessageHistory::WriteMedia(QDataStream &stream, Data::Media *media) {
	if (!media) {
		stream << qint32(MediaKind::None);
	} else if (const auto page = media->webpage()) {
		stream << qint32(MediaKind::WebPage)
			<< qint32(page->type) << page->url << page->displayUrl
			<< page->siteName << page->title << page->author
			<< qint32(page->duration) << qint32(media->webpageFlags().value());
		WriteText(stream, page->description);
		WritePhoto(stream, page->photo);
		WriteDocument(stream, page->document);
	} else if (const auto photo = media->photo()) {
		stream << qint32(MediaKind::Photo) << media->hasSpoiler();
		WritePhoto(stream, photo);
	} else if (const auto document = media->document()) {
		stream << qint32(MediaKind::Document) << media->hasSpoiler()
			<< qint32(media->videoTimestamp());
		WriteDocument(stream, document);
		WritePhoto(stream, media->videoCover());
	} else if (const auto contact = media->sharedContact()) {
		stream << qint32(MediaKind::Contact) << quint64(contact->userId.bare)
			<< contact->firstName << contact->lastName << contact->phoneNumber
			<< qint32(contact->vcardItems.size());
		for (const auto &[type, value] : contact->vcardItems) {
			stream << qint32(type) << value;
		}
	} else if (const auto poll = media->poll()) {
		stream << qint32(MediaKind::Poll) << qint32(poll->flags().value())
			<< qint32(poll->totalVoters);
		WriteText(stream, poll->question);
		WriteText(stream, poll->solution);
		stream << qint32(poll->answers.size());
		for (const auto &answer : poll->answers) {
			WriteText(stream, answer.text);
			stream << answer.option << qint32(answer.votes)
				<< answer.chosen << answer.correct;
		}
	} else if (const auto todo = media->todolist()) {
		stream << qint32(MediaKind::Todo);
		WriteText(stream, todo->title);
		stream << qint32(todo->items.size());
		for (const auto &task : todo->items) {
			stream << qint32(task.id) << qint32(task.completionDate);
			WritePeer(stream, task.completedBy);
			WriteText(stream, task.text);
		}
	} else if (media->location()) {
		const auto location = static_cast<Data::MediaLocation*>(media);
		stream << qint32(MediaKind::Location)
			<< location->_title << location->_description;
		WriteTL(stream, location->_point.toMTP());
	} else if (const auto dice = dynamic_cast<Data::MediaDice*>(media)) {
		const auto outcome = dice->diceGameOutcome();
		stream << qint32(MediaKind::Dice) << dice->emoji() << qint32(dice->value())
			<< qint64(outcome.nanoTon) << qint64(outcome.stakeNanoTon) << outcome.seed;
	} else if (const auto game = media->game()) {
		stream << qint32(MediaKind::Game) << quint64(game->accessHash)
			<< game->shortName << game->title << game->description;
		WritePhoto(stream, game->photo);
		WriteDocument(stream, game->document);
	} else if (const auto invoice = media->invoice()) {
		stream << qint32(MediaKind::Invoice) << qint64(invoice->receiptMsgId.bare)
			<< quint64(invoice->amount) << invoice->currency << invoice->title
			<< invoice->isPaidMedia << invoice->isTest;
		WriteText(stream, invoice->description);
		WritePhoto(stream, invoice->photo);
		stream << qint32(invoice->extendedMedia.size());
		for (const auto &part : invoice->extendedMedia) {
			WriteMedia(stream, part.get());
		}
	} else if (const auto story = media->storyId(); story.story) {
		stream << qint32(MediaKind::Story);
		WritePeer(stream, media->parent()->history()->owner().peer(story.peer));
		stream << qint32(story.story) << media->storyMention();
	} else if (const auto giveaway = media->giveawayStart()) {
		stream << qint32(MediaKind::GiveawayStart) << qint32(giveaway->untilDate)
			<< qint32(giveaway->quantity) << qint32(giveaway->months)
			<< quint64(giveaway->credits) << giveaway->all
			<< giveaway->additionalPrize << qint32(giveaway->countries.size());
		for (const auto &country : giveaway->countries) {
			stream << country;
		}
		stream << qint32(giveaway->channels.size());
		for (const auto channel : giveaway->channels) {
			WritePeer(stream, channel);
		}
	} else if (const auto giveaway = media->giveawayResults()) {
		stream << qint32(MediaKind::GiveawayResults);
		WritePeer(stream, giveaway->channel);
		stream << qint32(giveaway->untilDate) << qint64(giveaway->launchId.bare)
			<< qint32(giveaway->additionalPeersCount) << qint32(giveaway->winnersCount)
			<< qint32(giveaway->unclaimedCount) << qint32(giveaway->months)
			<< quint64(giveaway->credits) << giveaway->refunded << giveaway->all
			<< giveaway->additionalPrize << qint32(giveaway->winners.size());
		for (const auto winner : giveaway->winners) {
			WritePeer(stream, winner);
		}
	} else {
		stream << qint32(MediaKind::None);
	}
}

std::unique_ptr<Data::Media> MessageHistory::ReadMedia(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion) {
	const auto session = &item->history()->session();
	const auto archive = item->Get<HistoryMessageArchived>();
	auto kind = qint32(0);
	stream >> kind;
	switch (MediaKind(kind)) {
	case MediaKind::None: return nullptr;
	case MediaKind::Photo: {
		auto spoiler = false;
		stream >> spoiler;
		const auto photo = ReadPhoto(stream, session, appVersion);
		return photo ? std::make_unique<Data::MediaPhoto>(
			item,
			photo,
			Data::MediaPhoto::Args{ .spoiler = spoiler }) : nullptr;
	} break;
	case MediaKind::Document: {
		auto spoiler = false;
		auto timestamp = qint32(0);
		stream >> spoiler >> timestamp;
		const auto document = ReadDocument(stream, session, appVersion);
		const auto cover = ReadPhoto(stream, session, appVersion);
		return document ? std::make_unique<Data::MediaFile>(
			item,
			document,
			Data::MediaFile::Args{
				.videoCover = cover,
				.videoTimestamp = timestamp,
				.spoiler = spoiler,
			}) : nullptr;
	} break;
	case MediaKind::Contact: {
		auto id = quint64(0);
		auto first = QString();
		auto last = QString();
		auto phone = QString();
		auto count = qint32(0);
		stream >> id >> first >> last >> phone >> count;
		auto vcard = Data::SharedContact::VcardItems();
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			auto type = qint32(0);
			auto value = QString();
			stream >> type >> value;
			vcard.emplace(Data::SharedContact::VcardItemType(type), value);
		}
		return std::make_unique<Data::MediaContact>(
			item, UserId(id), first, last, phone, vcard);
	} break;
	case MediaKind::WebPage: {
		auto type = qint32(0);
		auto url = QString();
		auto display = QString();
		auto site = QString();
		auto title = QString();
		auto author = QString();
		auto duration = qint32(0);
		auto flags = qint32(0);
		stream >> type >> url >> display >> site >> title >> author
			>> duration >> flags;
		auto description = ReadText(stream);
		const auto photo = ReadPhoto(stream, session, appVersion);
		const auto document = ReadDocument(stream, session, appVersion);
		const auto page = archive->mediaLifetime.make_state<WebPageData>(
			&session->data(),
			item->id.bare);
		page->applyChanges(
			WebPageType(type),
			url,
			display,
			site,
			title,
			description,
			FullStoryId(),
			photo,
			document,
			WebPageCollage(),
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			0,
			duration,
			author,
			false,
			false,
			0);
		return std::make_unique<Data::MediaWebPage>(
			item,
			page,
			MediaWebPageFlags::from_raw(flags));
	} break;
	case MediaKind::Poll: {
		auto flags = qint32(0);
		auto votes = qint32(0);
		stream >> flags >> votes;
		const auto poll = archive->mediaLifetime.make_state<PollData>(
			&session->data(),
			item->id.bare);
		poll->setFlags(PollData::Flags::from_raw(flags) | PollData::Flag::Closed);
		poll->totalVoters = votes;
		poll->question = ReadText(stream);
		poll->solution = ReadText(stream);
		auto count = qint32(0);
		stream >> count;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			auto answer = PollAnswer();
			answer.text = ReadText(stream);
			stream >> answer.option >> answer.votes >> answer.chosen >> answer.correct;
			poll->answers.push_back(std::move(answer));
		}
		return std::make_unique<Data::MediaPoll>(item, poll);
	} break;
	case MediaKind::Todo: {
		const auto todo = archive->mediaLifetime.make_state<TodoListData>(
			&session->data(),
			item->fullId());
		todo->title = ReadText(stream);
		auto count = qint32(0);
		stream >> count;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			auto task = TodoListItem();
			stream >> task.id >> task.completionDate;
			const auto by = ReadPeer(stream, session, appVersion);
			task.completedBy = by ? session->data().peer(by).get() : nullptr;
			task.text = ReadText(stream);
			todo->items.push_back(std::move(task));
		}
		return std::make_unique<Data::MediaTodoList>(item, todo);
	} break;
	case MediaKind::Location: {
		auto title = QString();
		auto description = QString();
		stream >> title >> description;
		const auto geo = ReadTL<MTPGeoPoint>(stream);
		if (stream.status() != QDataStream::Ok || geo.type() != mtpc_geoPoint) {
			return nullptr;
		}
		return std::make_unique<Data::MediaLocation>(
			item, Data::LocationPoint(geo.c_geoPoint()), title, description);
	} break;
	case MediaKind::Dice: {
		auto emoji = QString();
		auto value = qint32(0);
		auto amount = qint64(0);
		auto stake = qint64(0);
		auto seed = QByteArray();
		stream >> emoji >> value >> amount >> stake >> seed;
		return std::make_unique<Data::MediaDice>(
			item, Data::DiceGameOutcome{ amount, stake, seed }, emoji, value);
	} break;
	case MediaKind::Game: {
		auto access = quint64(0);
		const auto game = archive->mediaLifetime.make_state<GameData>(
			&session->data(),
			item->id.bare);
		stream >> access >> game->shortName
			>> game->title >> game->description;
		game->accessHash = access;
		game->photo = ReadPhoto(stream, session, appVersion);
		game->document = ReadDocument(stream, session, appVersion);
		return std::make_unique<Data::MediaGame>(item, game);
	} break;
	case MediaKind::Invoice: {
		auto invoice = Data::Invoice();
		auto receipt = qint64(0);
		auto amount = quint64(0);
		stream >> receipt >> amount >> invoice.currency >> invoice.title
			>> invoice.isPaidMedia >> invoice.isTest;
		invoice.receiptMsgId = MsgId(receipt);
		invoice.amount = amount;
		invoice.description = ReadText(stream);
		invoice.photo = ReadPhoto(stream, session, appVersion);
		auto count = qint32(0);
		stream >> count;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			if (auto media = ReadMedia(stream, item, appVersion)) {
				invoice.extendedMedia.push_back(std::move(media));
			}
		}
		return std::make_unique<Data::MediaInvoice>(item, invoice);
	} break;
	case MediaKind::Story: {
		const auto peer = ReadPeer(stream, session, appVersion);
		auto id = qint32(0);
		auto mention = false;
		stream >> id >> mention;
		return std::make_unique<Data::MediaStory>(
			item, FullStoryId{ peer, StoryId(id) }, mention);
	} break;
	case MediaKind::GiveawayStart: {
		auto giveaway = Data::GiveawayStart();
		auto credits = quint64(0);
		auto count = qint32(0);
		stream >> giveaway.untilDate >> giveaway.quantity >> giveaway.months
			>> credits >> giveaway.all >> giveaway.additionalPrize >> count;
		giveaway.credits = credits;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			auto country = QString();
			stream >> country;
			giveaway.countries.push_back(std::move(country));
		}
		stream >> count;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			const auto peer = ReadPeer(stream, session, appVersion);
			if (const auto channel = session->data().peer(peer)->asChannel()) {
				giveaway.channels.push_back(channel);
			}
		}
		return std::make_unique<Data::MediaGiveawayStart>(item, giveaway);
	} break;
	case MediaKind::GiveawayResults: {
		const auto peer = ReadPeer(stream, session, appVersion);
		const auto channel = session->data().peer(peer)->asChannel();
		if (!channel) {
			stream.setStatus(QDataStream::ReadCorruptData);
			return nullptr;
		}
		auto giveaway = Data::GiveawayResults{ .channel = channel };
		auto launch = qint64(0);
		auto credits = quint64(0);
		auto count = qint32(0);
		stream >> giveaway.untilDate >> launch >> giveaway.additionalPeersCount
			>> giveaway.winnersCount >> giveaway.unclaimedCount >> giveaway.months
			>> credits >> giveaway.refunded >> giveaway.all
			>> giveaway.additionalPrize >> count;
		giveaway.launchId = MsgId(launch);
		giveaway.credits = credits;
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			giveaway.winners.push_back(session->data().peer(
				ReadPeer(stream, session, appVersion)));
		}
		return std::make_unique<Data::MediaGiveawayResults>(item, giveaway);
	} break;
	}
	stream.setStatus(QDataStream::ReadCorruptData);
	return nullptr;
}

QByteArray MessageHistory::Write(not_null<HistoryItem*> item) {
	auto writer = ByteArrayWriter();
	auto &stream = writer.underlying();
	stream << kFormat << qint32(AppVersion) << qint64(item->id.bare)
		<< qint32(item->date()) << item->out() << item->isPost()
		<< item->invertMedia();
	WritePeer(stream, item->from());
	WritePeer(stream, item->viaBot());
	const auto signedMessage = item->Get<HistoryMessageSigned>();
	const auto edited = item->Get<HistoryMessageEdited>();
	stream << (signedMessage ? signedMessage->author : QString())
		<< qint32(edited ? edited->date : 0);
	const auto reply = item->replyTo();
	const auto replyComponent = item->Get<HistoryMessageReply>();
	stream << bool(replyComponent)
		<< bool(replyComponent && replyComponent->topicPost())
		<< quint64(SerializePeerId(reply.messageId.peer))
		<< qint64(reply.messageId.msg.bare) << qint64(item->replyToTop().bare)
		<< quint64(SerializePeerId(reply.monoforumPeerId))
		<< qint32(reply.quoteOffset) << qint32(reply.todoItemId) << reply.pollOption
		<< quint64(SerializePeerId(reply.storyId.peer)) << qint32(reply.storyId.story);
	WriteText(stream, reply.quote);
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	stream << bool(forwarded);
	if (forwarded) {
		WritePeer(stream, forwarded->originalSender);
		WritePeer(stream, forwarded->savedFromPeer);
		WritePeer(stream, forwarded->savedFromSender);
		stream << qint32(forwarded->originalDate)
			<< qint64(forwarded->originalId.bare) << forwarded->originalPostAuthor
			<< forwarded->psaType << qint64(forwarded->savedFromMsgId.bare)
			<< qint32(forwarded->savedFromDate) << forwarded->savedFromOutgoing
			<< forwarded->imported << forwarded->story
			<< (forwarded->originalHiddenSenderInfo
				? forwarded->originalHiddenSenderInfo->name : QString())
			<< (forwarded->savedFromHiddenSenderInfo
				? forwarded->savedFromHiddenSenderInfo->name : QString());
	}
	WriteText(stream, item->originalText());
	WriteMedia(stream, item->media());
	WriteRich(stream, item);
	return std::move(writer).result();
}

MessageHistory::Item MessageHistory::Read(
		not_null<History*> history,
		const QByteArray &bytes,
		bool inlineCopy) {
	auto reader = ByteArrayReader(bytes);
	auto &stream = reader.underlying();
	auto format = qint32(0);
	auto appVersion = qint32(0);
	auto original = qint64(0);
	auto date = qint32(0);
	auto outgoing = false;
	auto post = false;
	auto invert = false;
	stream >> format >> appVersion >> original >> date >> outgoing >> post
		>> invert;
	if (format != kFormat) {
		return nullptr;
	}
	const auto session = &history->session();
	const auto from = ReadPeer(stream, session, appVersion);
	const auto via = ReadPeer(stream, session, appVersion);
	auto author = QString();
	auto edited = qint32(0);
	stream >> author >> edited;
	auto peer = quint64(0);
	auto message = qint64(0);
	auto top = qint64(0);
	auto sublist = quint64(0);
	auto storyPeer = quint64(0);
	auto story = qint32(0);
	auto hasReply = false;
	auto topicPost = false;
	auto reply = FullReplyTo();
	stream >> hasReply >> topicPost >> peer >> message >> top >> sublist
		>> reply.quoteOffset >> reply.todoItemId >> reply.pollOption
		>> storyPeer >> story;
	reply.messageId = FullMsgId(DeserializePeerId(peer), MsgId(message));
	reply.topicRootId = MsgId(top);
	reply.monoforumPeerId = DeserializePeerId(sublist);
	reply.storyId = { DeserializePeerId(storyPeer), StoryId(story) };
	reply.quote = ReadText(stream);
	const auto flags = MessageFlag::AdminLogEntry | MessageFlag::HasFromId
		| (outgoing ? MessageFlag::Outgoing : MessageFlag())
		| (post ? MessageFlag::Post : MessageFlag())
		| (invert ? MessageFlag::InvertMedia : MessageFlag())
		| (hasReply ? MessageFlag::HasReplyInfo : MessageFlag())
		| (!author.isEmpty() ? MessageFlag::HasPostAuthor : MessageFlag());
	auto owned = Item(history->makeMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = flags,
		.from = from,
		.replyTo = std::move(reply),
		.date = date,
		.viaBotId = peerToUser(via),
		.postAuthor = author,
	}, TextWithEntities(), MTP_messageMediaEmpty()).get());
	const auto item = owned.get();
	if (const auto reply = item->Get<HistoryMessageReply>()) {
		reply->updateFields(item, MsgId(message), MsgId(top), topicPost);
	}
	item->markArchived();
	const auto archive = item->Get<HistoryMessageArchived>();
	archive->original = FullMsgId(history->peer->id, MsgId(original));
	archive->sentAt = date;
	archive->inlineCopy = inlineCopy;
	if (edited) {
		item->AddComponents(HistoryMessageEdited::Bit());
		item->Get<HistoryMessageEdited>()->date = edited;
	}
	auto forwarded = false;
	stream >> forwarded;
	if (forwarded) {
		item->AddComponents(HistoryMessageForwarded::Bit());
		const auto data = item->Get<HistoryMessageForwarded>();
		const auto sender = ReadPeer(stream, session, appVersion);
		const auto savedPeer = ReadPeer(stream, session, appVersion);
		const auto savedSender = ReadPeer(stream, session, appVersion);
		data->originalSender = sender ? session->data().peer(sender).get() : nullptr;
		data->savedFromPeer = savedPeer ? session->data().peer(savedPeer).get() : nullptr;
		data->savedFromSender = savedSender ? session->data().peer(savedSender).get() : nullptr;
		auto originalId = qint64(0);
		auto savedId = qint64(0);
		auto hidden = QString();
		auto savedHidden = QString();
		stream >> data->originalDate >> originalId >> data->originalPostAuthor
			>> data->psaType >> savedId >> data->savedFromDate
			>> data->savedFromOutgoing >> data->imported >> data->story
			>> hidden >> savedHidden;
		data->originalId = MsgId(originalId);
		data->savedFromMsgId = MsgId(savedId);
		if (!hidden.isEmpty()) {
			data->originalHiddenSenderInfo = std::make_unique<HiddenSenderInfo>(hidden, false);
		}
		if (!savedHidden.isEmpty()) {
			data->savedFromHiddenSenderInfo = std::make_unique<HiddenSenderInfo>(savedHidden, false);
		}
	}
	auto text = ReadText(stream);
	auto media = ReadMedia(stream, item, appVersion);
	auto rich = ReadRich(stream, session, appVersion);
	if (stream.status() != QDataStream::Ok) {
		return nullptr;
	}
	item->applyContent({
		.text = std::move(text),
		.media = std::move(media),
		.richPage = std::move(rich),
		.invertMedia = invert,
	});
	return owned;
}

} // namespace Serialize
