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
#include "data/data_message_reaction_id.h"
#include "data/data_photo.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "iv/iv_data.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"

namespace Serialize {
namespace {

constexpr auto kFormat = qint32(5);

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
	Call,
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

void WritePeer(QDataStream &stream, PeerData *peer, bool full = true) {
	stream << quint64(peer ? SerializePeerId(peer->id) : 0);
	if (peer && full) {
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

void WriteMarkup(QDataStream &stream, not_null<HistoryItem*> item) {
	const auto markup = item->inlineReplyMarkup();
	stream << qint32(markup ? markup->data.rows.size() : 0);
	if (markup) {
		for (const auto &row : markup->data.rows) {
			stream << qint32(row.size());
			for (const auto &button : row) {
				stream << qint32(button.type) << button.text
					<< quint64(button.visual.iconId) << qint32(button.visual.color)
					<< button.data << button.forwardText << qint64(button.buttonId)
					<< qint32(button.peerTypes.value());
			}
		}
	}
}

HistoryMessageMarkupData ReadMarkup(QDataStream &stream) {
	auto result = HistoryMessageMarkupData();
	auto rows = qint32(0);
	stream >> rows;
	if (rows) {
		result.flags = ReplyMarkupFlag::Inline;
	}
	using Button = HistoryMessageMarkupButton;
	for (auto i = 0; i < rows && stream.status() == QDataStream::Ok; ++i) {
		auto &row = result.rows.emplace_back();
		auto count = qint32(0);
		stream >> count;
		for (auto j = 0; j < count && stream.status() == QDataStream::Ok; ++j) {
			auto type = qint32(0);
			auto text = QString();
			auto icon = quint64(0);
			auto color = qint32(0);
			auto data = QByteArray();
			auto forward = QString();
			auto id = qint64(0);
			auto peers = qint32(0);
			stream >> type >> text >> icon >> color >> data >> forward >> id >> peers;
			row.emplace_back(
				Button::Type(type),
				text,
				Button::Visual{ icon, Button::Color(color) },
				data,
				forward,
				id);
			row.back().peerTypes = InlineBots::PeerTypes::from_raw(peers);
		}
	}
	return result;
}

} // namespace

void MessageHistory::WritePhoto(
		QDataStream &stream,
		PhotoData *photo,
		Purpose purpose) {
	stream << quint64(photo ? photo->id : 0);
	if (!photo || purpose == Purpose::Content) {
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
		DocumentData *document,
		Purpose purpose) {
	stream << quint64(document ? document->id : 0);
	if (!document || purpose == Purpose::Content) {
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

void MessageHistory::WriteWebPage(
		QDataStream &stream,
		not_null<WebPageData*> page,
		qint32 flags,
		Purpose purpose) {
	stream << qint32(page->type) << page->url << page->displayUrl
		<< page->siteName << page->title << page->author
		<< qint32(page->duration) << flags;
	WriteText(stream, page->description);
	WritePhoto(stream, page->photo, purpose);
	WriteDocument(stream, page->document, purpose);
}

WebPageData *MessageHistory::ReadWebPage(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion,
		qint32 &flags) {
	const auto session = &item->history()->session();
	auto type = qint32(0);
	auto url = QString();
	auto display = QString();
	auto site = QString();
	auto title = QString();
	auto author = QString();
	auto duration = qint32(0);
	stream >> type >> url >> display >> site >> title >> author
		>> duration >> flags;
	auto description = ReadText(stream);
	const auto photo = ReadPhoto(stream, session, appVersion);
	const auto document = ReadDocument(stream, session, appVersion);
	const auto archive = item->Get<HistoryMessageArchived>();
	const auto page = archive->mediaLifetime->make_state<WebPageData>(
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
	return page;
}

void MessageHistory::WritePollMedia(
		QDataStream &stream,
		const PollMedia &media,
		Purpose purpose) {
	WritePhoto(stream, media.photo, purpose);
	WriteDocument(stream, media.document, purpose);
	stream << media.geo.has_value();
	if (media.geo) {
		WriteTL(stream, media.geo->toMTP());
	}
	stream << media.url << bool(media.webpage);
	if (media.webpage) {
		WriteWebPage(stream, media.webpage, 0, purpose);
	}
}

PollMedia MessageHistory::ReadPollMedia(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion) {
	const auto session = &item->history()->session();
	auto result = PollMedia();
	result.photo = ReadPhoto(stream, session, appVersion);
	result.document = ReadDocument(stream, session, appVersion);
	auto geo = false;
	stream >> geo;
	if (geo) {
		const auto point = ReadTL<MTPGeoPoint>(stream);
		if (stream.status() != QDataStream::Ok || point.type() != mtpc_geoPoint) {
			stream.setStatus(QDataStream::ReadCorruptData);
			return {};
		}
		result.geo = Data::LocationPoint(point.c_geoPoint());
	}
	auto webpage = false;
	stream >> result.url >> webpage;
	if (webpage) {
		auto flags = qint32(0);
		result.webpage = ReadWebPage(stream, item, appVersion, flags);
	}
	return result;
}

void MessageHistory::WriteRich(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		Purpose purpose) {
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
	if (purpose == Purpose::Content) {
		return;
	}
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

void MessageHistory::WriteMedia(
		QDataStream &stream,
		Data::Media *media,
		Purpose purpose) {
	const auto full = (purpose == Purpose::Snapshot);
	if (!media) {
		stream << qint32(MediaKind::None);
	} else if (const auto page = media->webpage()) {
		stream << qint32(MediaKind::WebPage);
		WriteWebPage(stream, page, qint32(media->webpageFlags().value()), purpose);
	} else if (const auto photo = media->photo()) {
		stream << qint32(MediaKind::Photo) << media->hasSpoiler();
		WritePhoto(stream, photo, purpose);
	} else if (const auto document = media->document()) {
		stream << qint32(MediaKind::Document) << media->hasSpoiler()
			<< qint32(media->videoTimestamp());
		WriteDocument(stream, document, purpose);
		WritePhoto(stream, media->videoCover(), purpose);
	} else if (const auto contact = media->sharedContact()) {
		stream << qint32(MediaKind::Contact) << quint64(contact->userId.bare)
			<< contact->firstName << contact->lastName << contact->phoneNumber
			<< qint32(contact->vcardItems.size());
		for (const auto &[type, value] : contact->vcardItems) {
			stream << qint32(type) << value;
		}
	} else if (const auto call = media->call()) {
		stream << qint32(MediaKind::Call) << quint64(call->conferenceId)
			<< qint32(call->duration) << qint32(call->state) << call->video
			<< qint32(call->otherParticipants.size());
		for (const auto peer : call->otherParticipants) {
			WritePeer(stream, peer, full);
		}
	} else if (const auto poll = media->poll()) {
		const auto flags = full
			? poll->flags()
			: poll->flags() & ~(PollData::Flag::Creator | PollData::Flag::CanViewStats);
		stream << qint32(MediaKind::Poll) << qint32(flags.value());
		if (full) {
			stream << qint32(poll->totalVoters);
		}
		WriteText(stream, poll->question);
		if (full) {
			WriteText(stream, poll->solution);
		}
		stream << qint32(poll->answers.size());
		for (const auto &answer : poll->answers) {
			WriteText(stream, answer.text);
			stream << answer.option;
			if (full) {
				stream << qint32(answer.votes) << answer.chosen << answer.correct;
			}
		}
		WritePollMedia(stream, poll->attachedMedia, purpose);
		if (full) {
			WritePollMedia(stream, poll->solutionMedia, purpose);
		}
		for (const auto &answer : poll->answers) {
			WritePollMedia(stream, answer.media, purpose);
		}
		stream << qint32(poll->closePeriod) << qint32(poll->closeDate)
			<< qint32(poll->countries.size());
		for (const auto &country : poll->countries) {
			stream << country;
		}
		if (full) {
			const auto voters = [&](const auto &list) {
				stream << qint32(list.size());
				for (const auto peer : list) {
					WritePeer(stream, peer);
				}
			};
			voters(poll->recentVoters);
			for (const auto &answer : poll->answers) {
				WritePeer(stream, answer.addedBy);
				stream << qint32(answer.addedDate);
				voters(answer.recentVoters);
			}
		}
	} else if (const auto todo = media->todolist()) {
		stream << qint32(MediaKind::Todo);
		WriteText(stream, todo->title);
		stream << qint32(todo->items.size());
		for (const auto &task : todo->items) {
			stream << qint32(task.id) << qint32(task.completionDate);
			WritePeer(stream, task.completedBy, full);
			WriteText(stream, task.text);
		}
		stream << qint32(todo->flags().value());
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
		WritePhoto(stream, game->photo, purpose);
		WriteDocument(stream, game->document, purpose);
	} else if (const auto invoice = media->invoice()) {
		stream << qint32(MediaKind::Invoice) << qint64(invoice->receiptMsgId.bare)
			<< quint64(invoice->amount) << invoice->currency << invoice->title
			<< invoice->isPaidMedia << invoice->isTest;
		WriteText(stream, invoice->description);
		WritePhoto(stream, invoice->photo, purpose);
		stream << qint32(invoice->extendedMedia.size());
		for (const auto &part : invoice->extendedMedia) {
			WriteMedia(stream, part.get(), purpose);
		}
	} else if (const auto story = media->storyId(); story.story) {
		stream << qint32(MediaKind::Story);
		WritePeer(stream, media->parent()->history()->owner().peer(story.peer), full);
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
			WritePeer(stream, channel, full);
		}
	} else if (const auto giveaway = media->giveawayResults()) {
		stream << qint32(MediaKind::GiveawayResults);
		WritePeer(stream, giveaway->channel, full);
		stream << qint32(giveaway->untilDate) << qint64(giveaway->launchId.bare)
			<< qint32(giveaway->additionalPeersCount) << qint32(giveaway->winnersCount)
			<< qint32(giveaway->unclaimedCount) << qint32(giveaway->months)
			<< quint64(giveaway->credits) << giveaway->refunded << giveaway->all
			<< giveaway->additionalPrize << qint32(giveaway->winners.size());
		for (const auto winner : giveaway->winners) {
			WritePeer(stream, winner, full);
		}
	} else {
		stream << qint32(MediaKind::None);
	}
}

std::unique_ptr<Data::Media> MessageHistory::ReadMedia(
		QDataStream &stream,
		not_null<HistoryItem*> item,
		int appVersion,
		int format) {
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
		if (!photo) {
			return nullptr;
		}
		return item->isService()
			? std::make_unique<Data::MediaPhoto>(item, item->history()->peer, photo)
			: std::make_unique<Data::MediaPhoto>(
				item, photo, Data::MediaPhoto::Args{ .spoiler = spoiler });
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
		auto flags = qint32(0);
		const auto page = ReadWebPage(stream, item, appVersion, flags);
		return std::make_unique<Data::MediaWebPage>(
			item,
			page,
			MediaWebPageFlags::from_raw(flags));
	} break;
	case MediaKind::Call: {
		auto call = Data::Call();
		auto conference = quint64(0);
		auto state = qint32(0);
		auto count = qint32(0);
		stream >> conference >> call.duration >> state >> call.video >> count;
		call.conferenceId = conference;
		call.state = Data::CallState(state);
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			call.otherParticipants.push_back(session->data().peer(
				ReadPeer(stream, session, appVersion)));
		}
		return std::make_unique<Data::MediaCall>(item, call);
	} break;
	case MediaKind::Poll: {
		auto flags = qint32(0);
		auto votes = qint32(0);
		stream >> flags >> votes;
		const auto poll = archive->mediaLifetime->make_state<PollData>(
			&session->data(),
			item->id.bare);
		poll->setFlags(PollData::Flags::from_raw(flags));
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
		if (format >= 4) {
			poll->attachedMedia = ReadPollMedia(stream, item, appVersion);
			poll->solutionMedia = ReadPollMedia(stream, item, appVersion);
			for (auto &answer : poll->answers) {
				answer.media = ReadPollMedia(stream, item, appVersion);
			}
		}
		if (format >= 5) {
			stream >> poll->closePeriod >> poll->closeDate >> count;
			for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
				auto country = QString();
				stream >> country;
				poll->countries.push_back(std::move(country));
			}
			const auto voters = [&](auto &list) {
				auto size = qint32(0);
				stream >> size;
				for (auto i = 0; i < size && stream.status() == QDataStream::Ok; ++i) {
					const auto peer = ReadPeer(stream, session, appVersion);
					if (peer) {
						list.push_back(session->data().peer(peer));
					}
				}
			};
			voters(poll->recentVoters);
			for (auto &answer : poll->answers) {
				const auto by = ReadPeer(stream, session, appVersion);
				answer.addedBy = by ? session->data().peer(by).get() : nullptr;
				stream >> answer.addedDate;
				voters(answer.recentVoters);
			}
		}
		++poll->version;
		return std::make_unique<Data::MediaPoll>(item, poll);
	} break;
	case MediaKind::Todo: {
		const auto todo = archive->mediaLifetime->make_state<TodoListData>(
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
		if (format >= 5) {
			auto flags = qint32(0);
			stream >> flags;
			todo->setFlags(TodoListData::Flags::from_raw(flags));
		}
		++todo->version;
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
		const auto game = archive->mediaLifetime->make_state<GameData>(
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
			if (auto media = ReadMedia(stream, item, appVersion, format)) {
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

TextWithEntities MessageHistory::Text(not_null<HistoryItem*> item) {
	auto result = item->_text;
	if (item->isService()) {
		if (const auto done = item->Get<HistoryServiceTodoCompletions>()
			; done && !done->completed.empty() && !done->incompleted.empty()) {
			result = item->composeTodoIncompleted(done).text
				.append(u"\n"_q).append(result);
		}
		result.entities.erase(ranges::remove_if(
			result.entities,
			[](const EntityInText &entity) {
				return entity.type() == EntityType::CustomUrl
					&& entity.data().startsWith(u"internal:"_q);
			}), result.entities.end());
	}
	return result;
}

QString MessageHistory::SearchText(not_null<HistoryItem*> item) {
	auto result = Text(item).text;
	if (const auto media = item->media()) {
		result += '\n' + media->clipboardText().rich.text;
	}
	if (const auto markup = item->inlineReplyMarkup()) {
		for (const auto &row : markup->data.rows) {
			for (const auto &button : row) {
				result += '\n' + button.text;
			}
		}
	}
	return result;
}

QByteArray MessageHistory::Content(not_null<HistoryItem*> item) {
	auto writer = ByteArrayWriter();
	auto &stream = writer.underlying();
	stream << item->isService() << item->invertMedia() << item->forbidsForward();
	WriteText(stream, Text(item));
	WriteMedia(stream, item->media(), Purpose::Content);
	WriteRich(stream, item, Purpose::Content);
	WriteMarkup(stream, item);
	return std::move(writer).result();
}

QByteArray MessageHistory::Write(not_null<HistoryItem*> item) {
	auto writer = ByteArrayWriter();
	auto &stream = writer.underlying();
	stream << kFormat << qint32(AppVersion) << qint64(item->id.bare)
		<< qint32(item->date()) << item->out() << item->isPost()
		<< item->invertMedia() << item->isService();
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
	WriteText(stream, Text(item));
	WriteMedia(stream, item->media());
	WriteRich(stream, item);
	stream << item->forbidsForward();
	WriteMarkup(stream, item);
	const auto views = item->Get<HistoryMessageViews>();
	stream << qint32(views ? views->views.count : -1)
		<< qint32(views ? views->forwardsCount : 0)
		<< qint32(views ? views->replies.count : -1)
		<< item->reactionsAreTags() << qint32(item->reactions().size());
	for (const auto &reaction : item->reactions()) {
		WriteTL(stream, Data::ReactionToMTP(reaction.id));
		stream << qint32(reaction.count) << reaction.my;
	}
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
	if (format < 3 || format > kFormat) {
		return nullptr;
	}
	auto service = false;
	if (format >= 5) {
		stream >> service;
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
	auto fields = HistoryItemCommonFields{
		.id = history->nextNonHistoryEntryId(),
		.flags = flags,
		.from = from,
		.replyTo = std::move(reply),
		.date = date,
		.viaBotId = peerToUser(via),
		.postAuthor = author,
	};
	auto owned = Item(service
		? history->makeMessage(std::move(fields), PreparedServiceText()).get()
		: history->makeMessage(
			std::move(fields), TextWithEntities(), MTP_messageMediaEmpty()).get());
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
	auto media = ReadMedia(stream, item, appVersion, format);
	auto rich = ReadRich(stream, session, appVersion);
	auto noForwards = false;
	if (!stream.atEnd()) {
		stream >> noForwards;
	}
	auto markup = HistoryMessageMarkupData();
	if (format >= 5) {
		markup = ReadMarkup(stream);
		auto views = qint32(0);
		auto forwards = qint32(0);
		auto replies = qint32(0);
		auto tags = false;
		auto count = qint32(0);
		stream >> views >> forwards >> replies >> tags >> count;
		if (views >= 0 || replies >= 0 || forwards > 0) {
			item->AddComponents(HistoryMessageViews::Bit());
			const auto data = item->Get<HistoryMessageViews>();
			data->views.count = views;
			data->forwardsCount = forwards;
			data->replies.count = replies;
		}
		auto reactions = QVector<MTPReactionCount>();
		for (auto i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
			const auto reaction = ReadTL<MTPReaction>(stream);
			auto total = qint32(0);
			auto mine = false;
			stream >> total >> mine;
			reactions.push_back(MTP_reactionCount(
				MTP_flags(mine
					? MTPDreactionCount::Flag::f_chosen_order
					: MTPDreactionCount::Flag()),
				MTP_int(0),
				reaction,
				MTP_int(total)));
		}
		if (stream.status() == QDataStream::Ok) {
			const auto data = MTPMessageReactions(MTP_messageReactions(
				MTP_flags(tags
					? MTPDmessageReactions::Flag::f_reactions_as_tags
					: MTPDmessageReactions::Flag()),
				MTP_vector<MTPReactionCount>(std::move(reactions)),
				MTP_vector<MTPMessagePeerReaction>(),
				MTP_vector<MTPMessageReactor>()));
			item->setReactions(&data);
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return nullptr;
	}
	if (noForwards) {
		item->_flags |= MessageFlag::NoForwards;
	}
	if (service) {
		item->setServiceText({ .text = std::move(text) });
		item->_media = std::move(media);
		item->setReplyMarkup(std::move(markup));
	} else {
		item->applyContent({
			.text = std::move(text),
			.media = std::move(media),
			.markup = std::move(markup),
			.richPage = std::move(rich),
			.invertMedia = invert,
		});
	}
	return owned;
}

} // namespace Serialize
