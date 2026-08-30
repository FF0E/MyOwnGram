// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_markup.h"

#include "core/local_url_handlers.h"
#include "lang/lang_keys.h"

#include <QtCore/QDataStream>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kReplyMarkupVisibleVersion = uint16(1);
constexpr auto kReplyMarkupSupportVersion = uint16(1);
constexpr auto kMinimumRowSize = int(sizeof(quint32));
constexpr auto kMinimumButtonSize = int(
	sizeof(quint8)
	+ sizeof(quint32)
	+ sizeof(quint64)
	+ sizeof(quint8)
	+ sizeof(quint32)
	+ sizeof(qint64)
	+ sizeof(quint8)
	+ sizeof(quint32));
constexpr auto kKnownPeerTypes = quint8(0x1F);
constexpr auto kKnownAdminRights = uint32(
	uint32(ChatAdminRight::ChangeInfo)
	| uint32(ChatAdminRight::PostMessages)
	| uint32(ChatAdminRight::EditMessages)
	| uint32(ChatAdminRight::DeleteMessages)
	| uint32(ChatAdminRight::BanUsers)
	| uint32(ChatAdminRight::InviteByLinkOrAdd)
	| uint32(ChatAdminRight::PinMessages)
	| uint32(ChatAdminRight::AddAdmins)
	| uint32(ChatAdminRight::Anonymous)
	| uint32(ChatAdminRight::ManageCall)
	| uint32(ChatAdminRight::Other)
	| uint32(ChatAdminRight::ManageTopics)
	| uint32(ChatAdminRight::PostStories)
	| uint32(ChatAdminRight::EditStories)
	| uint32(ChatAdminRight::DeleteStories)
	| uint32(ChatAdminRight::ManageDirect)
	| uint32(ChatAdminRight::ManageRanks)
	| uint32(ChatAdminRight::ProcessJoinRequests)
	| uint32(ChatAdminRight::ManageLinkedPeers));

struct FlagMapping {
	quint32 wireMask = 0;
	ReplyMarkupFlag flag = ReplyMarkupFlag::None;
	bool visible = false;
};

constexpr auto kFlags = std::array<FlagMapping, 12>{{
	{ 1U << 0, ReplyMarkupFlag::ForceReply, true },
	{ 1U << 1, ReplyMarkupFlag::HasSwitchInlineButton, false },
	{ 1U << 2, ReplyMarkupFlag::Inline, true },
	{ 1U << 3, ReplyMarkupFlag::Resize, true },
	{ 1U << 4, ReplyMarkupFlag::SingleUse, true },
	{ 1U << 5, ReplyMarkupFlag::Selective, true },
	{ 1U << 6, ReplyMarkupFlag::IsNull, true },
	{ 1U << 7, ReplyMarkupFlag::OnlyBuyButton, false },
	{ 1U << 8, ReplyMarkupFlag::Persistent, true },
	{ 1U << 9, ReplyMarkupFlag::SuggestionDecline, true },
	{ 1U << 10, ReplyMarkupFlag::SuggestionAccept, true },
	{ 1U << 11, ReplyMarkupFlag::SuggestionSeparator, true },
}};

struct ButtonTypeMapping {
	quint8 wireId = 0;
	HistoryMessageMarkupButton::Type type
		= HistoryMessageMarkupButton::Type::Default;
};

constexpr auto kButtonTypes = std::array<ButtonTypeMapping, 21>{{
	{ 1, HistoryMessageMarkupButton::Type::Default },
	{ 2, HistoryMessageMarkupButton::Type::Url },
	{ 3, HistoryMessageMarkupButton::Type::Callback },
	{ 4, HistoryMessageMarkupButton::Type::CallbackWithPassword },
	{ 5, HistoryMessageMarkupButton::Type::RequestPhone },
	{ 6, HistoryMessageMarkupButton::Type::RequestLocation },
	{ 7, HistoryMessageMarkupButton::Type::RequestPoll },
	{ 8, HistoryMessageMarkupButton::Type::RequestPeer },
	{ 9, HistoryMessageMarkupButton::Type::SwitchInline },
	{ 10, HistoryMessageMarkupButton::Type::SwitchInlineSame },
	{ 11, HistoryMessageMarkupButton::Type::Game },
	{ 12, HistoryMessageMarkupButton::Type::Buy },
	{ 13, HistoryMessageMarkupButton::Type::Auth },
	{ 14, HistoryMessageMarkupButton::Type::UserProfile },
	{ 15, HistoryMessageMarkupButton::Type::WebView },
	{ 16, HistoryMessageMarkupButton::Type::SimpleWebView },
	{ 17, HistoryMessageMarkupButton::Type::CopyText },
	{ 18, HistoryMessageMarkupButton::Type::SuggestDecline },
	{ 19, HistoryMessageMarkupButton::Type::SuggestAccept },
	{ 20, HistoryMessageMarkupButton::Type::SuggestChange },
	{ 21, HistoryMessageMarkupButton::Type::CreateBot },
}};

struct ButtonColorMapping {
	quint8 wireId = 0;
	HistoryMessageMarkupButton::Color color
		= HistoryMessageMarkupButton::Color::Normal;
};

constexpr auto kButtonColors = std::array<ButtonColorMapping, 4>{{
	{ 1, HistoryMessageMarkupButton::Color::Normal },
	{ 2, HistoryMessageMarkupButton::Color::Primary },
	{ 3, HistoryMessageMarkupButton::Color::Danger },
	{ 4, HistoryMessageMarkupButton::Color::Success },
}};

constexpr auto kKnownRuntimeFlags = [] {
	auto result = uint32();
	for (const auto &entry : kFlags) {
		result |= uint32(entry.flag);
	}
	return result;
}();

constexpr auto kKnownWireFlags = [] {
	auto result = quint32();
	for (const auto &entry : kFlags) {
		result |= entry.wireMask;
	}
	return result;
}();

std::optional<quint32> SerializeFlags(
		ReplyMarkupFlags flags,
		bool includeSupport) {
	if (uint32(flags.value()) & ~kKnownRuntimeFlags) {
		return std::nullopt;
	}
	auto result = quint32();
	for (const auto &entry : kFlags) {
		if ((includeSupport || entry.visible) && (flags & entry.flag)) {
			result |= entry.wireMask;
		}
	}
	return result;
}

std::optional<ReplyMarkupFlags> ParseFlags(quint32 flags) {
	if (flags & ~kKnownWireFlags) {
		return std::nullopt;
	}
	auto result = ReplyMarkupFlags();
	for (const auto &entry : kFlags) {
		if (flags & entry.wireMask) {
			result |= entry.flag;
		}
	}
	return result;
}

std::optional<quint8> SerializeButtonType(
		HistoryMessageMarkupButton::Type type) {
	const auto i = std::find_if(
		kButtonTypes.begin(),
		kButtonTypes.end(),
		[type](const auto &entry) { return entry.type == type; });
	if (i == kButtonTypes.end()) {
		return std::nullopt;
	}
	return i->wireId;
}

std::optional<HistoryMessageMarkupButton::Type> ParseButtonType(
		quint8 wireId) {
	const auto i = std::find_if(
		kButtonTypes.begin(),
		kButtonTypes.end(),
		[wireId](const auto &entry) { return entry.wireId == wireId; });
	if (i == kButtonTypes.end()) {
		return std::nullopt;
	}
	return i->type;
}

std::optional<quint8> SerializeButtonColor(
		HistoryMessageMarkupButton::Color color) {
	const auto i = std::find_if(
		kButtonColors.begin(),
		kButtonColors.end(),
		[color](const auto &entry) { return entry.color == color; });
	if (i == kButtonColors.end()) {
		return std::nullopt;
	}
	return i->wireId;
}

std::optional<HistoryMessageMarkupButton::Color> ParseButtonColor(
		quint8 wireId) {
	const auto i = std::find_if(
		kButtonColors.begin(),
		kButtonColors.end(),
		[wireId](const auto &entry) { return entry.wireId == wireId; });
	if (i == kButtonColors.end()) {
		return std::nullopt;
	}
	return i->color;
}

std::optional<quint8> SerializeRequestPeerType(
		RequestPeerQuery::Type type) {
	switch (type) {
	case RequestPeerQuery::Type::User: return 1;
	case RequestPeerQuery::Type::Group: return 2;
	case RequestPeerQuery::Type::Broadcast: return 3;
	}
	Unexpected("Type in RequestPeerQuery::Type.");
}

std::optional<RequestPeerQuery::Type> ParseRequestPeerType(quint8 type) {
	switch (type) {
	case 1: return RequestPeerQuery::Type::User;
	case 2: return RequestPeerQuery::Type::Group;
	case 3: return RequestPeerQuery::Type::Broadcast;
	}
	return std::nullopt;
}

std::optional<quint8> SerializeRestriction(
		RequestPeerQuery::Restriction restriction) {
	switch (restriction) {
	case RequestPeerQuery::Restriction::Any: return 1;
	case RequestPeerQuery::Restriction::Yes: return 2;
	case RequestPeerQuery::Restriction::No: return 3;
	}
	Unexpected("Type in RequestPeerQuery::Restriction.");
}

std::optional<RequestPeerQuery::Restriction> ParseRestriction(quint8 value) {
	switch (value) {
	case 1: return RequestPeerQuery::Restriction::Any;
	case 2: return RequestPeerQuery::Restriction::Yes;
	case 3: return RequestPeerQuery::Restriction::No;
	}
	return std::nullopt;
}

bool WriteRequestPeer(QDataStream &stream, const QByteArray &data) {
	if (data.size() != sizeof(RequestPeerQuery)) {
		return false;
	}
	auto query = RequestPeerQuery();
	std::memcpy(&query, data.constData(), sizeof(query));
	const auto type = SerializeRequestPeerType(query.type);
	const auto userIsBot = SerializeRestriction(query.userIsBot);
	const auto userIsPremium = SerializeRestriction(query.userIsPremium);
	const auto groupIsForum = SerializeRestriction(query.groupIsForum);
	const auto hasUsername = SerializeRestriction(query.hasUsername);
	const auto myRights = uint32(query.myRights.value());
	const auto botRights = uint32(query.botRights.value());
	if (!type
		|| !userIsBot
		|| !userIsPremium
		|| !groupIsForum
		|| !hasUsername
		|| query.maxQuantity < 0
		|| (myRights & ~kKnownAdminRights)
		|| (botRights & ~kKnownAdminRights)) {
		return false;
	}
	stream
		<< qint32(query.maxQuantity)
		<< *type
		<< *userIsBot
		<< *userIsPremium
		<< *groupIsForum
		<< *hasUsername
		<< quint8(query.amCreator ? 1 : 0)
		<< quint8(query.isBotParticipant ? 1 : 0)
		<< myRights
		<< botRights;
	return stream.status() == QDataStream::Ok;
}

std::optional<QByteArray> ReadRequestPeer(QDataStream &stream) {
	auto maxQuantity = qint32();
	auto type = quint8();
	auto userIsBot = quint8();
	auto userIsPremium = quint8();
	auto groupIsForum = quint8();
	auto hasUsername = quint8();
	auto amCreator = quint8();
	auto isBotParticipant = quint8();
	auto myRights = quint32();
	auto botRights = quint32();
	stream
		>> maxQuantity
		>> type
		>> userIsBot
		>> userIsPremium
		>> groupIsForum
		>> hasUsername
		>> amCreator
		>> isBotParticipant
		>> myRights
		>> botRights;
	const auto parsedType = ParseRequestPeerType(type);
	const auto parsedUserIsBot = ParseRestriction(userIsBot);
	const auto parsedUserIsPremium = ParseRestriction(userIsPremium);
	const auto parsedGroupIsForum = ParseRestriction(groupIsForum);
	const auto parsedHasUsername = ParseRestriction(hasUsername);
	if (stream.status() != QDataStream::Ok
		|| maxQuantity < 0
		|| !parsedType
		|| !parsedUserIsBot
		|| !parsedUserIsPremium
		|| !parsedGroupIsForum
		|| !parsedHasUsername
		|| amCreator > 1
		|| isBotParticipant > 1
		|| (myRights & ~kKnownAdminRights)
		|| (botRights & ~kKnownAdminRights)) {
		return std::nullopt;
	}
	auto query = RequestPeerQuery{
		.maxQuantity = maxQuantity,
		.type = *parsedType,
		.userIsBot = *parsedUserIsBot,
		.userIsPremium = *parsedUserIsPremium,
		.groupIsForum = *parsedGroupIsForum,
		.hasUsername = *parsedHasUsername,
		.amCreator = (amCreator != 0),
		.isBotParticipant = (isBotParticipant != 0),
		.myRights = ChatAdminRights::from_raw(myRights),
		.botRights = ChatAdminRights::from_raw(botRights),
	};
	return QByteArray(
		reinterpret_cast<const char*>(&query),
		sizeof(query));
}

bool WriteCreateBot(QDataStream &stream, const QByteArray &data) {
	auto source = QDataStream(data);
	auto suggestedName = QString();
	auto suggestedUsername = QString();
	source >> suggestedName >> suggestedUsername;
	return source.status() == QDataStream::Ok
		&& source.atEnd()
		&& Binary::WriteString(stream, suggestedName)
		&& Binary::WriteString(stream, suggestedUsername);
}

std::optional<QByteArray> ReadCreateBot(QDataStream &stream) {
	const auto suggestedName = Binary::ReadString(stream);
	const auto suggestedUsername = Binary::ReadString(stream);
	if (!suggestedName || !suggestedUsername) {
		return std::nullopt;
	}
	auto result = QByteArray();
	auto target = QDataStream(&result, QIODevice::WriteOnly);
	target << *suggestedName << *suggestedUsername;
	return (target.status() == QDataStream::Ok)
		? std::optional<QByteArray>(std::move(result))
		: std::nullopt;
}

bool WriteButtonData(
		QDataStream &stream,
		const HistoryMessageMarkupButton &button) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (button.type) {
	case Type::RequestPeer:
		return WriteRequestPeer(stream, button.data);
	case Type::CreateBot:
		return WriteCreateBot(stream, button.data);
	default:
		return Binary::WriteBytes(stream, button.data);
	}
}

std::optional<QByteArray> ReadButtonData(
		QDataStream &stream,
		HistoryMessageMarkupButton::Type type) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (type) {
	case Type::RequestPeer:
		return ReadRequestPeer(stream);
	case Type::CreateBot:
		return ReadCreateBot(stream);
	default:
		return Binary::ReadBytes(stream);
	}
}

std::optional<QByteArray> SerializePayload(
		const HistoryMessageMarkupData &markup,
		bool includeSupport) {
	const auto flags = SerializeFlags(markup.flags, includeSupport);
	if (!flags
		|| uint64(markup.rows.size())
			> std::numeric_limits<quint32>::max()) {
		return std::nullopt;
	}
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream << *flags;
	if (!Binary::WriteString(stream, markup.placeholder)) {
		return std::nullopt;
	}
	stream << quint32(markup.rows.size());
	for (const auto &row : markup.rows) {
		if (uint64(row.size()) > std::numeric_limits<quint32>::max()) {
			return std::nullopt;
		}
		stream << quint32(row.size());
		for (const auto &button : row) {
			const auto type = SerializeButtonType(button.type);
			const auto color = SerializeButtonColor(button.visual.color);
			if (!type || !color) {
				return std::nullopt;
			}
			stream << *type;
			if (!Binary::WriteString(stream, button.text)) {
				return std::nullopt;
			}
			stream
				<< quint64(button.visual.iconId)
				<< *color;
			if (!includeSupport
				&& button.type == HistoryMessageMarkupButton::Type::Url) {
				stream << quint8(Core::IsMiniAppUrl(
					QString::fromUtf8(button.data)) ? 1 : 0);
			}
			if (includeSupport) {
				const auto peerTypes = quint8(button.peerTypes.value());
				if ((peerTypes & ~kKnownPeerTypes)
					|| !Binary::WriteString(stream, button.forwardText)) {
					return std::nullopt;
				}
				stream
					<< qint64(button.buttonId)
					<< peerTypes;
				if (!WriteButtonData(stream, button)) {
					return std::nullopt;
				}
			}
		}
	}
	return (stream.status() == QDataStream::Ok)
		? std::optional<QByteArray>(std::move(result))
		: std::nullopt;
}

ParsedReplyMarkup ParseFailure(ParseError error) {
	return { .error = error };
}

} // namespace

std::optional<SerializedReplyMarkup> SerializeReplyMarkup(
		const HistoryMessageMarkupData *markup) {
	if (!markup || markup->isNull()) {
		return SerializedReplyMarkup();
	}
	const auto visible = SerializePayload(*markup, false);
	const auto support = SerializePayload(*markup, true);
	if (!visible || !support) {
		return std::nullopt;
	}
	auto serializedVisible = SerializeRecord(
		RecordType::ReplyMarkupVisible,
		kReplyMarkupVisibleVersion,
		*visible);
	auto serializedSupport = SerializeRecord(
		RecordType::ReplyMarkupSupport,
		kReplyMarkupSupportVersion,
		*support);
	if (!serializedVisible || !serializedSupport) {
		return std::nullopt;
	}
	return SerializedReplyMarkup{
		.visible = std::move(*serializedVisible),
		.support = std::move(*serializedSupport),
	};
}

ParsedReplyMarkup ParseReplyMarkup(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {
			.value = HistoryMessageMarkupData(),
			.error = ParseError::None,
		};
	}
	const auto record = ParseRecord(
		serialized,
		RecordType::ReplyMarkupSupport,
		kReplyMarkupSupportVersion);
	if (!record) {
		return ParseFailure(record.error);
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto flags = quint32();
	stream >> flags;
	const auto parsedFlags = ParseFlags(flags);
	const auto placeholder = Binary::ReadString(stream);
	auto rowCount = quint32();
	stream >> rowCount;
	if (stream.status() != QDataStream::Ok
		|| !parsedFlags
		|| !placeholder
		|| rowCount > quint64(stream.device()->bytesAvailable())
			/ kMinimumRowSize) {
		return ParseFailure(ParseError::Corrupt);
	}
	auto result = HistoryMessageMarkupData();
	result.flags = *parsedFlags;
	result.placeholder = *placeholder;
	result.rows.reserve(rowCount);
	for (auto i = quint32(); i != rowCount; ++i) {
		auto buttonCount = quint32();
		stream >> buttonCount;
		if (stream.status() != QDataStream::Ok
			|| buttonCount > quint64(stream.device()->bytesAvailable())
				/ kMinimumButtonSize) {
			return ParseFailure(ParseError::Corrupt);
		}
		auto row = std::vector<HistoryMessageMarkupButton>();
		row.reserve(buttonCount);
		for (auto j = quint32(); j != buttonCount; ++j) {
			auto type = quint8();
			stream >> type;
			const auto parsedType = ParseButtonType(type);
			const auto text = Binary::ReadString(stream);
			auto iconId = quint64();
			auto color = quint8();
			stream >> iconId >> color;
			const auto parsedColor = ParseButtonColor(color);
			const auto forwardText = Binary::ReadString(stream);
			auto buttonId = qint64();
			auto peerTypes = quint8();
			stream >> buttonId >> peerTypes;
			if (stream.status() != QDataStream::Ok
				|| !parsedType
				|| !text
				|| !parsedColor
				|| !forwardText
				|| (peerTypes & ~kKnownPeerTypes)) {
				return ParseFailure(ParseError::Corrupt);
			}
			const auto data = ReadButtonData(stream, *parsedType);
			if (!data) {
				return ParseFailure(ParseError::Corrupt);
			}
			row.emplace_back(
				*parsedType,
				*text,
				HistoryMessageMarkupButton::Visual{
					.iconId = DocumentId(iconId),
					.color = *parsedColor,
				},
				*data,
				*forwardText,
				buttonId);
			row.back().peerTypes
				= InlineBots::PeerTypes::from_raw(peerTypes);
		}
		result.rows.push_back(std::move(row));
	}
	if (stream.status() != QDataStream::Ok
		|| !stream.atEnd()
		|| (result.flags & ReplyMarkupFlag::IsNull)) {
		return ParseFailure(ParseError::Corrupt);
	}
	return {
		.value = std::move(result),
		.error = ParseError::None,
	};
}

#ifdef _DEBUG
void ValidateReplyMarkupFormat() {
	static const auto validated = [] {
		for (const auto &entry : kFlags) {
			auto flag = ReplyMarkupFlags();
			flag |= entry.flag;
			Assert(SerializeFlags(flag, true) == entry.wireMask);
			Assert(ParseFlags(entry.wireMask) == flag);
		}
		for (const auto &entry : kButtonTypes) {
			Assert(SerializeButtonType(entry.type) == entry.wireId);
			Assert(ParseButtonType(entry.wireId) == entry.type);
		}
		for (const auto &entry : kButtonColors) {
			Assert(SerializeButtonColor(entry.color) == entry.wireId);
			Assert(ParseButtonColor(entry.wireId) == entry.color);
		}
		auto text = QString();
		text.append(QChar(0xD800));
		auto markup = HistoryMessageMarkupData();
		markup.flags = ReplyMarkupFlag::Inline;
		markup.rows.push_back({ HistoryMessageMarkupButton(
			HistoryMessageMarkupButton::Type::Url,
			text,
			{
				.iconId = DocumentId(42),
				.color = HistoryMessageMarkupButton::Color::Primary,
			},
			"https://example.com") });
		const auto serialized = SerializeReplyMarkup(&markup);
		Assert(serialized.has_value());
		const auto parsed = ParseReplyMarkup(serialized->support);
		Assert(parsed);
		Assert(parsed.value.flags == markup.flags);
		Assert(parsed.value.rows.size() == 1);
		Assert(parsed.value.rows.front().size() == 1);
		const auto &button = parsed.value.rows.front().front();
		Assert(button.type == markup.rows.front().front().type);
		Assert(button.text == text);
		Assert(button.visual.iconId == DocumentId(42));
		Assert(button.visual.color
			== HistoryMessageMarkupButton::Color::Primary);
		Assert(button.data == "https://example.com");
		auto changedSupport = markup;
		changedSupport.rows.front().front().data
			= "https://example.org";
		const auto supportChanged = SerializeReplyMarkup(&changedSupport);
		Assert(supportChanged.has_value());
		Assert(supportChanged->visible == serialized->visible);
		Assert(supportChanged->support != serialized->support);
		auto changedIcon = markup;
		changedIcon.rows.front().front().data
			= "tg://resolve?domain=testbot&appname=test";
		const auto iconChanged = SerializeReplyMarkup(&changedIcon);
		Assert(iconChanged.has_value());
		Assert(iconChanged->visible != serialized->visible);
		auto changedVisible = markup;
		changedVisible.rows.front().front().text = u"changed"_q;
		const auto visibleChanged = SerializeReplyMarkup(&changedVisible);
		Assert(visibleChanged.has_value());
		Assert(visibleChanged->visible != serialized->visible);
		auto query = RequestPeerQuery{
			.maxQuantity = 2,
			.type = RequestPeerQuery::Type::Group,
			.groupIsForum = RequestPeerQuery::Restriction::Yes,
			.amCreator = true,
		};
		query.myRights |= ChatAdminRight::ManageTopics;
		auto queryData = QByteArray(
			reinterpret_cast<const char*>(&query),
			sizeof(query));
		auto queryPayload = QByteArray();
		auto queryWriter = QDataStream(
			&queryPayload,
			QIODevice::WriteOnly);
		queryWriter.setByteOrder(QDataStream::BigEndian);
		Assert(WriteRequestPeer(queryWriter, queryData));
		auto queryReader = QDataStream(queryPayload);
		queryReader.setByteOrder(QDataStream::BigEndian);
		const auto parsedQueryData = ReadRequestPeer(queryReader);
		Assert(parsedQueryData.has_value());
		Assert(queryReader.atEnd());
		auto parsedQuery = RequestPeerQuery();
		std::memcpy(
			&parsedQuery,
			parsedQueryData->constData(),
			sizeof(parsedQuery));
		Assert(parsedQuery.maxQuantity == 2);
		Assert(parsedQuery.type == RequestPeerQuery::Type::Group);
		Assert(parsedQuery.groupIsForum
			== RequestPeerQuery::Restriction::Yes);
		Assert(parsedQuery.amCreator);
		Assert(parsedQuery.myRights & ChatAdminRight::ManageTopics);
		Assert(!ParseReplyMarkup(serialized->support.chopped(1)));
		return true;
	}();
	static_cast<void>(validated);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
