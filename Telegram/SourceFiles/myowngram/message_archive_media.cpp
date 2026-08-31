// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_media.h"

#include "core/file_location.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "ui/image/image_location.h"

#include <QtCore/QDataStream>

#include <limits>
#include <vector>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kPhotoVisibleVersion = uint16(1);
constexpr auto kPhotoSupportVersion = uint16(1);
constexpr auto kDocumentVisibleVersion = uint16(1);
constexpr auto kDocumentSupportVersion = uint16(1);
constexpr auto kMaxDimension = int32(100000);

struct ImageReference {
	QByteArray location;
	int32 width = 0;
	int32 height = 0;
	int32 bytesCount = 0;
};

struct PhotoSnapshot {
	uint64 id = 0;
	bool spoiler = false;
	bool extendedPreview = false;
	int32 width = 0;
	int32 height = 0;
	bool video = false;
	crl::time videoStartTime = 0;
	std::optional<TimeId> extendedVideoDuration;
	TimeId date = 0;
	bool hasStickers = false;
	QByteArray inlineThumbnail;
	ImageReference small;
	ImageReference thumbnail;
	ImageReference large;
	ImageReference videoSmall;
	ImageReference videoLarge;
};

[[nodiscard]] bool GoodDimension(int32 value) {
	return value >= 0 && value <= kMaxDimension;
}

[[nodiscard]] QByteArray PersistedLocation(
		const ImageLocation &location) {
	if (!location.valid()
		|| std::holds_alternative<InMemoryLocation>(location.file().data)) {
		return {};
	}
	return location.file().serialize();
}

[[nodiscard]] ImageReference MakeImageReference(
		const ImageLocation &location,
		int bytesCount) {
	return {
		.location = PersistedLocation(location),
		.width = location.width(),
		.height = location.height(),
		.bytesCount = bytesCount,
	};
}

[[nodiscard]] bool GoodImageReference(const ImageReference &image) {
	if (!GoodDimension(image.width)
		|| !GoodDimension(image.height)
		|| image.bytesCount < 0) {
		return false;
	}
	if (image.location.isEmpty()) {
		return true;
	}
	const auto location = DownloadLocation::FromSerialized(image.location);
	return location
		&& location->valid()
		&& !std::holds_alternative<InMemoryLocation>(location->data);
}

[[nodiscard]] bool WriteImageReference(
		QDataStream &stream,
		const ImageReference &image) {
	if (!GoodImageReference(image)) {
		return false;
	}
	stream
		<< qint32(image.width)
		<< qint32(image.height)
		<< qint32(image.bytesCount);
	return Binary::WriteBytes(stream, image.location);
}

[[nodiscard]] std::optional<ImageReference> ReadImageReference(
		QDataStream &stream) {
	auto result = ImageReference();
	qint32 width = 0;
	qint32 height = 0;
	qint32 bytesCount = 0;
	stream
		>> width
		>> height
		>> bytesCount;
	const auto location = Binary::ReadBytes(stream);
	if (!location) {
		return std::nullopt;
	}
	result = {
		.location = *location,
		.width = width,
		.height = height,
		.bytesCount = bytesCount,
	};
	return GoodImageReference(result)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

[[nodiscard]] std::optional<QByteArray> SerializePhotoVisible(
		const PhotoSnapshot &photo) {
	if ((!photo.id && !photo.extendedPreview)
		|| (photo.id && photo.extendedPreview)
		|| !GoodDimension(photo.width)
		|| !GoodDimension(photo.height)
		|| photo.videoStartTime < 0
		|| (photo.extendedVideoDuration
			&& (!photo.extendedPreview
				|| !photo.video
				|| *photo.extendedVideoDuration < 0))) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< quint64(photo.id)
		<< quint8(photo.spoiler ? 1 : 0)
		<< quint8(photo.extendedPreview ? 1 : 0)
		<< qint32(photo.width)
		<< qint32(photo.height)
		<< quint8(photo.video ? 1 : 0)
		<< qint64(photo.videoStartTime)
		<< quint8(photo.extendedVideoDuration ? 1 : 0);
	if (photo.extendedVideoDuration) {
		stream << qint32(*photo.extendedVideoDuration);
	}
	if (photo.extendedPreview
		&& !Binary::WriteBytes(stream, photo.inlineThumbnail)) {
		return std::nullopt;
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::PhotoMediaVisible,
		kPhotoVisibleVersion,
		payload);
}

[[nodiscard]] std::optional<QByteArray> SerializePhotoSupport(
		const PhotoSnapshot &photo) {
	if (photo.date < 0
		|| (photo.extendedPreview && photo.date != 0)) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< qint32(photo.date)
		<< quint8(photo.hasStickers ? 1 : 0);
	if (!Binary::WriteBytes(
			stream,
			photo.extendedPreview ? QByteArray() : photo.inlineThumbnail)) {
		return std::nullopt;
	}
	if (!WriteImageReference(stream, photo.small)
		|| !WriteImageReference(stream, photo.thumbnail)
		|| !WriteImageReference(stream, photo.large)
		|| !WriteImageReference(stream, photo.videoSmall)
		|| !WriteImageReference(stream, photo.videoLarge)) {
		return std::nullopt;
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::PhotoMediaSupport,
		kPhotoSupportVersion,
		payload);
}

[[nodiscard, maybe_unused]] std::optional<PhotoSnapshot> ParsePhoto(
		const QByteArray &visible,
		const QByteArray &support) {
	const auto visibleRecord = ParseRecord(
		visible,
		RecordType::PhotoMediaVisible,
		kPhotoVisibleVersion);
	const auto supportRecord = ParseRecord(
		support,
		RecordType::PhotoMediaSupport,
		kPhotoSupportVersion);
	if (!visibleRecord || !supportRecord) {
		return std::nullopt;
	}
	auto result = PhotoSnapshot();
	auto visibleStream = QDataStream(visibleRecord.payload);
	visibleStream.setVersion(QDataStream::Qt_5_1);
	visibleStream.setByteOrder(QDataStream::BigEndian);
	quint64 id = 0;
	quint8 spoiler = 0;
	quint8 extendedPreview = 0;
	qint32 width = 0;
	qint32 height = 0;
	quint8 video = 0;
	qint64 videoStartTime = 0;
	quint8 hasExtendedVideoDuration = 0;
	visibleStream
		>> id
		>> spoiler
		>> extendedPreview
		>> width
		>> height
		>> video
		>> videoStartTime
		>> hasExtendedVideoDuration;
	if (spoiler > 1
		|| extendedPreview > 1
		|| video > 1
		|| hasExtendedVideoDuration > 1) {
		return std::nullopt;
	}
	result.id = id;
	result.spoiler = (spoiler != 0);
	result.extendedPreview = (extendedPreview != 0);
	result.width = width;
	result.height = height;
	result.video = (video != 0);
	result.videoStartTime = videoStartTime;
	if (hasExtendedVideoDuration) {
		qint32 duration = 0;
		visibleStream >> duration;
		if (duration < 0) {
			return std::nullopt;
		}
		result.extendedVideoDuration = duration;
	}
	if (result.extendedPreview) {
		const auto thumbnail = Binary::ReadBytes(visibleStream);
		if (!thumbnail) {
			return std::nullopt;
		}
		result.inlineThumbnail = *thumbnail;
	}
	if (visibleStream.status() != QDataStream::Ok
		|| !visibleStream.atEnd()
		|| (!result.id && !result.extendedPreview)
		|| (result.id && result.extendedPreview)
		|| !GoodDimension(result.width)
		|| !GoodDimension(result.height)
		|| result.videoStartTime < 0
		|| (result.extendedVideoDuration
			&& (!result.extendedPreview || !result.video))) {
		return std::nullopt;
	}
	auto supportStream = QDataStream(supportRecord.payload);
	supportStream.setVersion(QDataStream::Qt_5_1);
	supportStream.setByteOrder(QDataStream::BigEndian);
	qint32 date = 0;
	quint8 hasStickers = 0;
	supportStream >> date >> hasStickers;
	const auto inlineThumbnail = Binary::ReadBytes(supportStream);
	if (!inlineThumbnail
		|| date < 0
		|| hasStickers > 1
		|| (result.extendedPreview && date != 0)) {
		return std::nullopt;
	}
	result.date = date;
	result.hasStickers = (hasStickers != 0);
	if (!result.extendedPreview) {
		result.inlineThumbnail = *inlineThumbnail;
	} else if (!inlineThumbnail->isEmpty()) {
		return std::nullopt;
	}
	const auto small = ReadImageReference(supportStream);
	const auto thumbnail = ReadImageReference(supportStream);
	const auto large = ReadImageReference(supportStream);
	const auto videoSmall = ReadImageReference(supportStream);
	const auto videoLarge = ReadImageReference(supportStream);
	if (!small || !thumbnail || !large || !videoSmall || !videoLarge) {
		return std::nullopt;
	}
	result.small = *small;
	result.thumbnail = *thumbnail;
	result.large = *large;
	result.videoSmall = *videoSmall;
	result.videoLarge = *videoLarge;
	return (supportStream.status() == QDataStream::Ok
		&& supportStream.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

[[nodiscard]] std::optional<SerializedPhotoMedia> SerializePhoto(
		const PhotoSnapshot &photo) {
	const auto visible = SerializePhotoVisible(photo);
	const auto support = SerializePhotoSupport(photo);
	return (visible && support)
		? std::make_optional(SerializedPhotoMedia{
			.visible = *visible,
			.support = *support,
		})
		: std::nullopt;
}

[[nodiscard]] std::optional<SerializedPhotoMedia> SerializePhotoData(
		const PhotoData *photo,
		bool spoiler) {
	if (!photo) {
		return std::nullopt;
	}
	using Size = Data::PhotoSize;
	const auto image = [&](Size size) {
		return photo->hasExact(size)
			? MakeImageReference(
				photo->location(size),
				photo->imageByteSize(size))
			: ImageReference();
	};
	const auto video = photo->hasVideo()
		|| photo->extendedMediaVideoDuration().has_value();
	const auto extendedPreview = photo->extendedMediaPreview();
	return SerializePhoto(
		PhotoSnapshot{
			.id = extendedPreview ? PhotoId(0) : photo->id,
			.spoiler = spoiler,
			.extendedPreview = extendedPreview,
			.width = photo->width(),
			.height = photo->height(),
			.video = video,
			.videoStartTime = photo->videoStartPosition(),
			.extendedVideoDuration = photo->extendedMediaVideoDuration(),
			.date = photo->date(),
			.hasStickers = photo->hasAttachedStickers(),
			.inlineThumbnail = photo->inlineThumbnailBytes(),
			.small = image(Size::Small),
			.thumbnail = image(Size::Thumbnail),
			.large = image(Size::Large),
			.videoSmall = photo->hasVideoSmall()
				? MakeImageReference(
					photo->videoLocation(Size::Small),
					photo->videoByteSize(Size::Small))
				: ImageReference(),
			.videoLarge = MakeImageReference(
				photo->videoLocation(Size::Large),
				photo->videoByteSize(Size::Large)),
		});
}

enum class DocumentIdentityKind : uint8 {
	Remote = 1,
	Cache = 2,
};

enum class WireDocumentType : uint8 {
	File = 1,
	Video = 2,
	Song = 3,
	Sticker = 4,
	Animated = 5,
	Voice = 6,
	RoundVideo = 7,
	WallPaper = 8,
};

enum class WireStickerType : uint8 {
	Webp = 1,
	Tgs = 2,
	Webm = 3,
};

enum class WireStickerSetType : uint8 {
	Stickers = 1,
	Masks = 2,
	Emoji = 3,
};

struct StickerSnapshot {
	StickerType type = StickerType::Webp;
	Data::StickersType setType = Data::StickersType::Stickers;
	QString alt;
	bool premium = false;
	bool textColor = false;
	StickerSetIdentifier set;
};

struct SongSnapshot {
	QString title;
	QString performer;
};

struct LocalFileReference {
	QString path;
	qint64 modified = 0;
	qint64 size = 0;
	QByteArray bookmark;
};

struct DocumentSnapshot {
	DocumentIdentityKind identityKind = DocumentIdentityKind::Remote;
	Storage::Cache::Key identity;
	DocumentType type = FileDocument;
	qint64 size = 0;
	int32 width = 0;
	int32 height = 0;
	QString filename;
	QString mime;
	std::optional<crl::time> duration;
	bool spoiler = false;
	crl::time ttl = 0;
	TimeId videoTimestamp = 0;
	bool silentVideo = false;
	bool skipPremiumEffect = false;
	std::optional<StickerSnapshot> sticker;
	std::optional<SongSnapshot> song;
	QByteArray coverVisible;
	TimeId date = 0;
	QByteArray remoteLocation;
	Storage::Cache::Key cacheKey;
	uint8 cacheTag = 0;
	std::optional<LocalFileReference> localFile;
	bool inlineThumbnailIsPath = false;
	QByteArray inlineThumbnail;
	ImageReference thumbnail;
	ImageReference videoThumbnail;
	bool supportsStreaming = false;
	bool hasAttachedStickers = false;
	bool hasQualitiesList = false;
	std::vector<SerializedDocumentMedia> qualities;
	QByteArray waveform;
	QString videoCodec;
	int32 videoPreloadPrefix = 0;
	QByteArray coverSupport;
};

[[nodiscard]] std::optional<WireDocumentType> DocumentTypeToWire(
		DocumentType type) {
	switch (type) {
	case FileDocument: return WireDocumentType::File;
	case VideoDocument: return WireDocumentType::Video;
	case SongDocument: return WireDocumentType::Song;
	case StickerDocument: return WireDocumentType::Sticker;
	case AnimatedDocument: return WireDocumentType::Animated;
	case VoiceDocument: return WireDocumentType::Voice;
	case RoundVideoDocument: return WireDocumentType::RoundVideo;
	case WallPaperDocument: return WireDocumentType::WallPaper;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<DocumentType> DocumentTypeFromWire(
		quint8 value) {
	switch (WireDocumentType(value)) {
	case WireDocumentType::File: return FileDocument;
	case WireDocumentType::Video: return VideoDocument;
	case WireDocumentType::Song: return SongDocument;
	case WireDocumentType::Sticker: return StickerDocument;
	case WireDocumentType::Animated: return AnimatedDocument;
	case WireDocumentType::Voice: return VoiceDocument;
	case WireDocumentType::RoundVideo: return RoundVideoDocument;
	case WireDocumentType::WallPaper: return WallPaperDocument;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<WireStickerType> StickerTypeToWire(
		StickerType type) {
	switch (type) {
	case StickerType::Webp: return WireStickerType::Webp;
	case StickerType::Tgs: return WireStickerType::Tgs;
	case StickerType::Webm: return WireStickerType::Webm;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<StickerType> StickerTypeFromWire(
		quint8 value) {
	switch (WireStickerType(value)) {
	case WireStickerType::Webp: return StickerType::Webp;
	case WireStickerType::Tgs: return StickerType::Tgs;
	case WireStickerType::Webm: return StickerType::Webm;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<WireStickerSetType> StickerSetTypeToWire(
		Data::StickersType type) {
	switch (type) {
	case Data::StickersType::Stickers:
		return WireStickerSetType::Stickers;
	case Data::StickersType::Masks:
		return WireStickerSetType::Masks;
	case Data::StickersType::Emoji:
		return WireStickerSetType::Emoji;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Data::StickersType> StickerSetTypeFromWire(
		quint8 value) {
	switch (WireStickerSetType(value)) {
	case WireStickerSetType::Stickers:
		return Data::StickersType::Stickers;
	case WireStickerSetType::Masks:
		return Data::StickersType::Masks;
	case WireStickerSetType::Emoji:
		return Data::StickersType::Emoji;
	}
	return std::nullopt;
}

[[nodiscard]] bool GoodDocumentIdentity(
		const DocumentSnapshot &document) {
	switch (document.identityKind) {
	case DocumentIdentityKind::Remote:
		return !document.identity.high && document.identity.low;
	case DocumentIdentityKind::Cache:
		return document.identity.valid();
	}
	return false;
}

[[nodiscard]] bool GoodWaveform(const QByteArray &waveform) {
	for (const auto value : waveform) {
		if (uchar(value) > 31) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool GoodDocumentDimensions(int32 width, int32 height) {
	return ((width == -1) && (height == -1))
		|| (GoodDimension(width) && GoodDimension(height));
}

[[nodiscard]] bool GoodDocumentVisible(
		const DocumentSnapshot &document) {
	const auto sticker = (document.type == StickerDocument);
	const auto song = (document.type == SongDocument);
	return GoodDocumentIdentity(document)
		&& DocumentTypeToWire(document.type).has_value()
		&& document.size >= 0
		&& GoodDocumentDimensions(document.width, document.height)
		&& (!document.duration || *document.duration >= 0)
		&& document.ttl >= 0
		&& document.videoTimestamp >= 0
		&& (sticker == document.sticker.has_value())
		&& (song == document.song.has_value())
		&& (!document.sticker
			|| (StickerTypeToWire(document.sticker->type)
				&& StickerSetTypeToWire(document.sticker->setType)));
}

[[nodiscard]] bool GoodRemoteLocation(
		const DocumentSnapshot &document) {
	if (document.identityKind == DocumentIdentityKind::Cache) {
		return document.remoteLocation.isEmpty();
	}
	const auto location = StorageFileLocation::FromSerialized(
		document.remoteLocation);
	return location
		&& location->valid()
		&& location->type() == StorageFileLocation::Type::Document
		&& location->objectId() == document.identity.low
		&& location->cacheKey() == document.cacheKey;
}

[[nodiscard]] bool GoodDocumentSupport(
		const DocumentSnapshot &document) {
	const auto voice = (document.type == VoiceDocument)
		|| (document.type == RoundVideoDocument);
	const auto video = (document.type == VideoDocument);
	const auto cacheIdentityMatches = document.identityKind
		!= DocumentIdentityKind::Cache
		|| (document.identity.high == document.cacheKey.high
			&& document.identity.low == document.cacheKey.low);
	const auto localFileGood = !document.localFile
		|| (!document.localFile->path.isEmpty()
			&& document.localFile->size >= 0);
	const auto coverGood = document.coverVisible.isEmpty()
		|| ParsePhoto(document.coverVisible, document.coverSupport).has_value();
	return document.date >= 0
		&& GoodRemoteLocation(document)
		&& document.cacheKey.valid()
		&& cacheIdentityMatches
		&& localFileGood
		&& GoodImageReference(document.thumbnail)
		&& GoodImageReference(document.videoThumbnail)
		&& GoodWaveform(document.waveform)
		&& (voice || document.waveform.isEmpty())
		&& (video || document.videoCodec.isEmpty())
		&& document.videoPreloadPrefix >= 0
		&& (video || document.videoPreloadPrefix == 0)
		&& (video
			|| (!document.hasQualitiesList && document.qualities.empty()))
		&& (document.hasQualitiesList || document.qualities.empty())
		&& document.qualities.size()
			<= std::numeric_limits<quint32>::max()
		&& (document.coverVisible.isEmpty()
			== document.coverSupport.isEmpty())
		&& coverGood;
}

[[nodiscard]] std::optional<QByteArray> SerializeDocumentVisible(
		const DocumentSnapshot &document) {
	const auto type = DocumentTypeToWire(document.type);
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream
		<< quint8(document.identityKind)
		<< quint64(document.identity.high)
		<< quint64(document.identity.low)
		<< quint8(*type)
		<< qint64(document.size)
		<< qint32(document.width)
		<< qint32(document.height)
		<< qint64(document.duration.value_or(-1))
		<< quint8(document.spoiler ? 1 : 0)
		<< qint64(document.ttl)
		<< qint32(document.videoTimestamp)
		<< quint8(document.silentVideo ? 1 : 0)
		<< quint8(document.skipPremiumEffect ? 1 : 0);
	if (!Binary::WriteString(stream, document.filename)
		|| !Binary::WriteString(stream, document.mime)) {
		return std::nullopt;
	}
	if (const auto &sticker = document.sticker) {
		const auto stickerType = StickerTypeToWire(sticker->type);
		const auto setType = StickerSetTypeToWire(sticker->setType);
		stream
			<< quint8(*stickerType)
			<< quint8(*setType)
			<< quint8(sticker->premium ? 1 : 0)
			<< quint8(sticker->textColor ? 1 : 0);
		if (!Binary::WriteString(stream, sticker->alt)) {
			return std::nullopt;
		}
	} else if (const auto &song = document.song) {
		if (!Binary::WriteString(stream, song->title)
			|| !Binary::WriteString(stream, song->performer)) {
			return std::nullopt;
		}
	}
	if (!Binary::WriteBytes(stream, document.coverVisible)
		|| stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::DocumentMediaVisible,
		kDocumentVisibleVersion,
		payload);
}

[[nodiscard]] std::optional<QByteArray> SerializeDocumentSupport(
		const DocumentSnapshot &document) {
	auto payload = QByteArray();
	auto stream = QDataStream(&payload, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	stream << qint32(document.date);
	if (!Binary::WriteBytes(stream, document.remoteLocation)) {
		return std::nullopt;
	}
	stream
		<< quint64(document.cacheKey.high)
		<< quint64(document.cacheKey.low)
		<< quint8(document.cacheTag)
		<< quint8(document.localFile ? 1 : 0);
	if (const auto &local = document.localFile) {
		if (!Binary::WriteString(stream, local->path)) {
			return std::nullopt;
		}
		stream
			<< qint64(local->modified)
			<< qint64(local->size);
		if (!Binary::WriteBytes(stream, local->bookmark)) {
			return std::nullopt;
		}
	}
	stream << quint8(document.inlineThumbnailIsPath ? 1 : 0);
	if (!Binary::WriteBytes(stream, document.inlineThumbnail)
		|| !WriteImageReference(stream, document.thumbnail)
		|| !WriteImageReference(stream, document.videoThumbnail)) {
		return std::nullopt;
	}
	stream
		<< quint8(document.supportsStreaming ? 1 : 0)
		<< quint8(document.hasAttachedStickers ? 1 : 0)
		<< quint8(document.hasQualitiesList ? 1 : 0);
	if (const auto &sticker = document.sticker) {
		stream
			<< quint64(sticker->set.id)
			<< quint64(sticker->set.accessHash);
		if (!Binary::WriteString(stream, sticker->set.shortName)) {
			return std::nullopt;
		}
	}
	if (document.type == VoiceDocument
		|| document.type == RoundVideoDocument) {
		if (!Binary::WriteBytes(stream, document.waveform)) {
			return std::nullopt;
		}
	} else if (document.type == VideoDocument
		&& !Binary::WriteString(stream, document.videoCodec)) {
		return std::nullopt;
	}
	stream << qint32(document.videoPreloadPrefix);
	if (!Binary::WriteBytes(stream, document.coverSupport)) {
		return std::nullopt;
	}
	stream << quint32(document.qualities.size());
	for (const auto &quality : document.qualities) {
		if (!Binary::WriteBytes(stream, quality.visible)
			|| !Binary::WriteBytes(stream, quality.support)) {
			return std::nullopt;
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return SerializeRecord(
		RecordType::DocumentMediaSupport,
		kDocumentSupportVersion,
		payload);
}

[[nodiscard]] std::optional<DocumentSnapshot> ParseDocumentVisible(
		const QByteArray &serialized) {
	const auto record = ParseRecord(
		serialized,
		RecordType::DocumentMediaVisible,
		kDocumentVisibleVersion);
	if (!record) {
		return std::nullopt;
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto identityKind = quint8();
	auto identityHigh = quint64();
	auto identityLow = quint64();
	auto typeValue = quint8();
	auto size = qint64();
	auto width = qint32();
	auto height = qint32();
	auto duration = qint64();
	auto spoiler = quint8();
	auto ttl = qint64();
	auto videoTimestamp = qint32();
	auto silentVideo = quint8();
	auto skipPremiumEffect = quint8();
	stream
		>> identityKind
		>> identityHigh
		>> identityLow
		>> typeValue
		>> size
		>> width
		>> height
		>> duration
		>> spoiler
		>> ttl
		>> videoTimestamp
		>> silentVideo
		>> skipPremiumEffect;
	const auto type = DocumentTypeFromWire(typeValue);
	const auto filename = Binary::ReadString(stream);
	const auto mime = Binary::ReadString(stream);
	if (!type
		|| !filename
		|| !mime
		|| (identityKind != quint8(DocumentIdentityKind::Remote)
			&& identityKind != quint8(DocumentIdentityKind::Cache))
		|| duration < -1
		|| spoiler > 1
		|| silentVideo > 1
		|| skipPremiumEffect > 1) {
		return std::nullopt;
	}
	auto result = DocumentSnapshot{
		.identityKind = DocumentIdentityKind(identityKind),
		.identity = { identityHigh, identityLow },
		.type = *type,
		.size = size,
		.width = width,
		.height = height,
		.filename = *filename,
		.mime = *mime,
		.duration = (duration >= 0)
			? std::make_optional(crl::time(duration))
			: std::nullopt,
		.spoiler = (spoiler != 0),
		.ttl = crl::time(ttl),
		.videoTimestamp = videoTimestamp,
		.silentVideo = (silentVideo != 0),
		.skipPremiumEffect = (skipPremiumEffect != 0),
	};
	if (result.type == StickerDocument) {
		auto stickerTypeValue = quint8();
		auto setTypeValue = quint8();
		auto premium = quint8();
		auto textColor = quint8();
		stream
			>> stickerTypeValue
			>> setTypeValue
			>> premium
			>> textColor;
		const auto stickerType = StickerTypeFromWire(stickerTypeValue);
		const auto setType = StickerSetTypeFromWire(setTypeValue);
		const auto alt = Binary::ReadString(stream);
		if (!stickerType
			|| !setType
			|| !alt
			|| premium > 1
			|| textColor > 1) {
			return std::nullopt;
		}
		result.sticker = StickerSnapshot{
			.type = *stickerType,
			.setType = *setType,
			.alt = *alt,
			.premium = (premium != 0),
			.textColor = (textColor != 0),
		};
	} else if (result.type == SongDocument) {
		const auto title = Binary::ReadString(stream);
		const auto performer = Binary::ReadString(stream);
		if (!title || !performer) {
			return std::nullopt;
		}
		result.song = SongSnapshot{
			.title = *title,
			.performer = *performer,
		};
	}
	const auto cover = Binary::ReadBytes(stream);
	if (!cover) {
		return std::nullopt;
	}
	result.coverVisible = *cover;
	return (stream.status() == QDataStream::Ok && stream.atEnd())
		? std::make_optional(std::move(result))
		: std::nullopt;
}

[[nodiscard, maybe_unused]] std::optional<DocumentSnapshot> ParseDocument(
		const QByteArray &visible,
		const QByteArray &support,
		bool allowQualities) {
	auto result = ParseDocumentVisible(visible);
	const auto record = ParseRecord(
		support,
		RecordType::DocumentMediaSupport,
		kDocumentSupportVersion);
	if (!result || !record) {
		return std::nullopt;
	}
	auto stream = QDataStream(record.payload);
	stream.setVersion(QDataStream::Qt_5_1);
	stream.setByteOrder(QDataStream::BigEndian);
	auto date = qint32();
	stream >> date;
	const auto remoteLocation = Binary::ReadBytes(stream);
	auto cacheHigh = quint64();
	auto cacheLow = quint64();
	auto cacheTag = quint8();
	auto hasLocalFile = quint8();
	stream
		>> cacheHigh
		>> cacheLow
		>> cacheTag
		>> hasLocalFile;
	if (!remoteLocation || hasLocalFile > 1) {
		return std::nullopt;
	}
	result->date = date;
	result->remoteLocation = *remoteLocation;
	result->cacheKey = { cacheHigh, cacheLow };
	result->cacheTag = cacheTag;
	if (hasLocalFile) {
		const auto path = Binary::ReadString(stream);
		auto modified = qint64();
		auto size = qint64();
		stream >> modified >> size;
		const auto bookmark = Binary::ReadBytes(stream);
		if (!path || !bookmark) {
			return std::nullopt;
		}
		result->localFile = LocalFileReference{
			.path = *path,
			.modified = modified,
			.size = size,
			.bookmark = *bookmark,
		};
	}
	auto inlineThumbnailIsPath = quint8();
	stream >> inlineThumbnailIsPath;
	const auto inlineThumbnail = Binary::ReadBytes(stream);
	const auto thumbnail = ReadImageReference(stream);
	const auto videoThumbnail = ReadImageReference(stream);
	auto supportsStreaming = quint8();
	auto hasAttachedStickers = quint8();
	auto hasQualitiesList = quint8();
	stream
		>> supportsStreaming
		>> hasAttachedStickers
		>> hasQualitiesList;
	if (inlineThumbnailIsPath > 1
		|| !inlineThumbnail
		|| !thumbnail
		|| !videoThumbnail
		|| supportsStreaming > 1
		|| hasAttachedStickers > 1
		|| hasQualitiesList > 1) {
		return std::nullopt;
	}
	result->inlineThumbnailIsPath = (inlineThumbnailIsPath != 0);
	result->inlineThumbnail = *inlineThumbnail;
	result->thumbnail = *thumbnail;
	result->videoThumbnail = *videoThumbnail;
	result->supportsStreaming = (supportsStreaming != 0);
	result->hasAttachedStickers = (hasAttachedStickers != 0);
	result->hasQualitiesList = (hasQualitiesList != 0);
	if (result->sticker) {
		auto setId = quint64();
		auto setAccessHash = quint64();
		stream >> setId >> setAccessHash;
		const auto setShortName = Binary::ReadString(stream);
		if (!setShortName) {
			return std::nullopt;
		}
		result->sticker->set = {
			.id = setId,
			.accessHash = setAccessHash,
			.shortName = *setShortName,
		};
	}
	if (result->type == VoiceDocument
		|| result->type == RoundVideoDocument) {
		const auto waveform = Binary::ReadBytes(stream);
		if (!waveform) {
			return std::nullopt;
		}
		result->waveform = *waveform;
	} else if (result->type == VideoDocument) {
		const auto codec = Binary::ReadString(stream);
		if (!codec) {
			return std::nullopt;
		}
		result->videoCodec = *codec;
	}
	auto preloadPrefix = qint32();
	stream >> preloadPrefix;
	result->videoPreloadPrefix = preloadPrefix;
	const auto coverSupport = Binary::ReadBytes(stream);
	auto qualitiesCount = quint32();
	stream >> qualitiesCount;
	if (!coverSupport
		|| stream.status() != QDataStream::Ok
		|| qualitiesCount
			> quint64(stream.device()->bytesAvailable()) / 8
		|| (!allowQualities
			&& (result->hasQualitiesList || qualitiesCount))) {
		return std::nullopt;
	}
	result->coverSupport = *coverSupport;
	result->qualities.reserve(qualitiesCount);
	for (auto i = quint32(); i != qualitiesCount; ++i) {
		const auto visible = Binary::ReadBytes(stream);
		const auto support = Binary::ReadBytes(stream);
		if (!visible || !support) {
			return std::nullopt;
		}
		const auto parsed = ParseDocument(*visible, *support, false);
		if (!parsed || parsed->type != VideoDocument) {
			return std::nullopt;
		}
		result->qualities.push_back({ *visible, *support });
	}
	return (stream.status() == QDataStream::Ok
		&& stream.atEnd()
		&& GoodDocumentVisible(*result)
		&& GoodDocumentSupport(*result))
		? result
		: std::nullopt;
}

[[nodiscard]] std::optional<SerializedDocumentMedia> SerializeDocument(
		const DocumentSnapshot &document) {
	if (!GoodDocumentVisible(document)
		|| !GoodDocumentSupport(document)) {
		return std::nullopt;
	}
	const auto visible = SerializeDocumentVisible(document);
	const auto support = SerializeDocumentSupport(document);
	return (visible && support)
		? std::make_optional(
			SerializedDocumentMedia{
				.visible = *visible,
				.support = *support,
			})
		: std::nullopt;
}

[[nodiscard]] std::optional<LocalFileReference> LocalFile(
		const DocumentData *document) {
	const auto &location = document->location(true);
	return (!location.isEmpty() && !location.inMediaCache())
		? std::make_optional(
			LocalFileReference{
				.path = location.fname,
				.modified = location.modified.toMSecsSinceEpoch(),
				.size = location.size,
				.bookmark = location.bookmark(),
			})
		: std::nullopt;
}

[[nodiscard]] QByteArray ResolvedWaveform(
		const DocumentData *document) {
	const auto voice = document->voice()
		? document->voice()
		: document->round();
	if (!voice || voice->waveform.isEmpty()) {
		return {};
	}
	for (const auto value : voice->waveform) {
		if (value < 0) {
			return {};
		}
	}
	return QByteArray(
		reinterpret_cast<const char*>(voice->waveform.constData()),
		voice->waveform.size());
}

[[nodiscard]] std::optional<DocumentSnapshot> DocumentFromData(
		const DocumentData *document) {
	if (!document) {
		return std::nullopt;
	}
	const auto cacheKey = document->cacheKey();
	if (!cacheKey.valid()) {
		return std::nullopt;
	}
	const auto remote = document->hasRemoteLocation();
	auto result = DocumentSnapshot{
		.identityKind = remote
			? DocumentIdentityKind::Remote
			: DocumentIdentityKind::Cache,
		.identity = remote
			? Storage::Cache::Key{ 0, document->id }
			: cacheKey,
		.type = document->type,
		.size = document->size,
		.width = document->dimensions.width(),
		.height = document->dimensions.height(),
		.filename = document->filename(),
		.mime = document->mimeString(),
		.duration = document->hasDuration()
			? std::make_optional(document->duration())
			: std::nullopt,
		.silentVideo = document->isSilentVideo(),
		.date = document->date,
		.remoteLocation = remote
			? document->videoPreloadLocation().serialize()
			: QByteArray(),
		.cacheKey = cacheKey,
		.cacheTag = document->cacheTag(),
		.localFile = LocalFile(document),
		.inlineThumbnailIsPath = document->inlineThumbnailIsPath(),
		.inlineThumbnail = document->inlineThumbnailBytes(),
		.thumbnail = MakeImageReference(
			document->thumbnailLocation(),
			document->thumbnailByteSize()),
		.videoThumbnail = MakeImageReference(
			document->videoThumbnailLocation(),
			document->videoThumbnailByteSize()),
		.supportsStreaming = document->supportsStreaming(),
		.hasAttachedStickers = document->hasAttachedStickers(),
		.waveform = ResolvedWaveform(document),
		.videoCodec = document->video()
			? document->video()->codec
			: QString(),
		.videoPreloadPrefix = document->isVideoFile()
			? document->videoPreloadPrefix()
			: 0,
	};
	if (const auto sticker = document->sticker()) {
		result.sticker = StickerSnapshot{
			.type = sticker->type,
			.setType = sticker->setType,
			.alt = sticker->alt,
			.premium = document->isPremiumSticker()
				|| document->isPremiumEmoji(),
			.textColor = document->emojiUsesTextColor(),
			.set = sticker->set,
		};
	} else if (const auto song = document->song()) {
		result.song = SongSnapshot{
			.title = song->title,
			.performer = song->performer,
		};
	}
	return result;
}

[[nodiscard]] std::optional<DocumentSnapshot> DocumentFromMedia(
		const Data::Media *media) {
	const auto document = (media && !media->webpage())
		? media->document()
		: nullptr;
	auto result = DocumentFromData(document);
	if (!result) {
		return std::nullopt;
	}
	result->spoiler = media->hasSpoiler();
	result->ttl = media->ttlSeconds();
	result->videoTimestamp = media->videoTimestamp();
	result->skipPremiumEffect = media->skipPremiumEffect();
	if (const auto cover = media->videoCover()) {
		const auto serialized = SerializePhotoData(cover, false);
		if (!serialized) {
			return std::nullopt;
		}
		result->coverVisible = serialized->visible;
		result->coverSupport = serialized->support;
	}
	if (media->hasQualitiesList()) {
		const auto video = document->video();
		if (!video) {
			return std::nullopt;
		}
		result->hasQualitiesList = true;
		for (const auto quality : video->qualities) {
			if (quality == document) {
				continue;
			}
			const auto snapshot = DocumentFromData(quality);
			if (!snapshot || snapshot->type != VideoDocument) {
				return std::nullopt;
			}
			const auto serialized = SerializeDocument(*snapshot);
			if (!serialized) {
				return std::nullopt;
			}
			result->qualities.push_back(*serialized);
		}
	}
	return result;
}

} // namespace

std::optional<SerializedPhotoMedia> SerializePhotoMedia(
		const Data::Media *media) {
	return (media && !media->webpage())
		? SerializePhotoData(media->photo(), media->hasSpoiler())
		: std::nullopt;
}

std::optional<SerializedDocumentMedia> SerializeDocumentMedia(
		const Data::Media *media) {
	const auto document = DocumentFromMedia(media);
	return document
		? SerializeDocument(*document)
		: std::nullopt;
}

#ifdef _DEBUG
void ValidatePhotoMediaFormat() {
	static const auto checked = [] {
		auto remote = DownloadLocation();
		remote.data = PlainUrlLocation{ u"https://example.com/photo.jpg"_q };
		const auto location = remote.serialize();
		const auto original = PhotoSnapshot{
			.id = 0,
			.spoiler = true,
			.extendedPreview = true,
			.width = 320,
			.height = 240,
			.video = true,
			.videoStartTime = 1500,
			.extendedVideoDuration = 12,
			.date = 0,
			.hasStickers = false,
			.inlineThumbnail = QByteArray("preview"),
			.small = {
				.location = location,
				.width = 90,
				.height = 90,
				.bytesCount = 100,
			},
		};
		const auto serialized = SerializePhoto(original);
		Assert(serialized.has_value());
		const auto parsed = ParsePhoto(
			serialized->visible,
			serialized->support);
		Assert(parsed.has_value());
		const auto serializedAgain = SerializePhoto(*parsed);
		Assert(serializedAgain.has_value());
		Assert(serializedAgain->visible == serialized->visible);
		Assert(serializedAgain->support == serialized->support);
		auto invalidPreview = original;
		invalidPreview.id = 42;
		Assert(!SerializePhoto(invalidPreview).has_value());
		auto regular = original;
		regular.id = 42;
		regular.extendedPreview = false;
		regular.extendedVideoDuration.reset();
		regular.date = 1;
		Assert(SerializePhoto(regular).has_value());
		auto supportChanged = original;
		supportChanged.small.bytesCount++;
		const auto changedSupport = SerializePhoto(supportChanged);
		Assert(changedSupport.has_value());
		Assert(changedSupport->visible == serialized->visible);
		Assert(changedSupport->support != serialized->support);
		auto visibleChanged = original;
		visibleChanged.spoiler = false;
		const auto changedVisible = SerializePhoto(visibleChanged);
		Assert(changedVisible.has_value());
		Assert(changedVisible->visible != serialized->visible);
		return true;
	}();
	Q_UNUSED(checked);
}

namespace {

[[nodiscard]] bool CheckDocumentMediaFormat() {
	const auto checkDocumentType = [](DocumentType type) {
		const auto wire = DocumentTypeToWire(type);
		Assert(wire.has_value());
		Assert(DocumentTypeFromWire(quint8(*wire)) == type);
	};
	checkDocumentType(FileDocument);
	checkDocumentType(VideoDocument);
	checkDocumentType(SongDocument);
	checkDocumentType(StickerDocument);
	checkDocumentType(AnimatedDocument);
	checkDocumentType(VoiceDocument);
	checkDocumentType(RoundVideoDocument);
	checkDocumentType(WallPaperDocument);
	Assert(!DocumentTypeFromWire(0).has_value());
	const auto checkStickerType = [](StickerType type) {
		const auto wire = StickerTypeToWire(type);
		Assert(wire.has_value());
		Assert(StickerTypeFromWire(quint8(*wire)) == type);
	};
	checkStickerType(StickerType::Webp);
	checkStickerType(StickerType::Tgs);
	checkStickerType(StickerType::Webm);
	Assert(!StickerTypeFromWire(0).has_value());
	const auto checkSetType = [](Data::StickersType type) {
		const auto wire = StickerSetTypeToWire(type);
		Assert(wire.has_value());
		Assert(StickerSetTypeFromWire(quint8(*wire)) == type);
	};
	checkSetType(Data::StickersType::Stickers);
	checkSetType(Data::StickersType::Masks);
	checkSetType(Data::StickersType::Emoji);
	Assert(!StickerSetTypeFromWire(0).has_value());
	auto remote = DownloadLocation();
	remote.data = PlainUrlLocation{ u"https://example.com/video.jpg"_q };
	const auto cover = SerializePhoto(
		PhotoSnapshot{
			.id = 42,
			.width = 640,
			.height = 360,
			.date = 1,
		});
	Assert(cover.has_value());
	const auto original = DocumentSnapshot{
		.identityKind = DocumentIdentityKind::Cache,
		.identity = { 11, 12 },
		.type = VideoDocument,
		.size = 123456,
		.width = 1280,
		.height = 720,
		.filename = u"video.mp4"_q,
		.mime = u"video/mp4"_q,
		.duration = 45000,
		.spoiler = true,
		.videoTimestamp = 3,
		.silentVideo = true,
		.coverVisible = cover->visible,
		.date = 1,
		.cacheKey = { 11, 12 },
		.cacheTag = 4,
		.localFile = LocalFileReference{
			.path = u"/tmp/video.mp4"_q,
			.modified = 100,
			.size = 123456,
			.bookmark = QByteArray("bookmark"),
		},
		.inlineThumbnail = QByteArray("inline"),
		.thumbnail = {
			.location = remote.serialize(),
			.width = 320,
			.height = 180,
			.bytesCount = 100,
		},
		.videoThumbnail = {
			.location = remote.serialize(),
			.width = 480,
			.height = 270,
			.bytesCount = 200,
		},
		.supportsStreaming = true,
		.hasAttachedStickers = true,
		.videoCodec = u"h264"_q,
		.videoPreloadPrefix = 4096,
		.coverSupport = cover->support,
	};
	const auto serialized = SerializeDocument(original);
	Assert(serialized.has_value());
	const auto parsed = ParseDocument(
		serialized->visible,
		serialized->support,
		true);
	Assert(parsed.has_value());
	Assert(parsed->thumbnail.width == 320);
	Assert(parsed->thumbnail.height == 180);
	Assert(parsed->videoThumbnail.width == 480);
	Assert(parsed->videoThumbnail.height == 270);
	const auto serializedAgain = SerializeDocument(*parsed);
	Assert(serializedAgain.has_value());
	Assert(serializedAgain->visible == serialized->visible);
	Assert(serializedAgain->support == serialized->support);
	const auto checkRoundtrip = [](const DocumentSnapshot &document) {
		const auto serialized = SerializeDocument(document);
		Assert(serialized.has_value());
		const auto parsed = ParseDocument(
			serialized->visible,
			serialized->support,
			true);
		Assert(parsed.has_value());
		const auto serializedAgain = SerializeDocument(*parsed);
		Assert(serializedAgain.has_value());
		Assert(serializedAgain->visible == serialized->visible);
		Assert(serializedAgain->support == serialized->support);
	};
	auto remoteDocument = original;
	remoteDocument.identityKind = DocumentIdentityKind::Remote;
	remoteDocument.identity = { 0, 42 };
	const auto remoteDocumentLocation = StorageFileLocation(
		2,
		UserId(1),
		MTP_inputDocumentFileLocation(
			MTP_long(42),
			MTP_long(43),
			MTP_bytes(QByteArray("reference")),
			MTP_string()));
	remoteDocument.remoteLocation = remoteDocumentLocation.serialize();
	remoteDocument.cacheKey = remoteDocumentLocation.cacheKey();
	checkRoundtrip(remoteDocument);
	auto wrongRemoteCacheKey = remoteDocument;
	wrongRemoteCacheKey.cacheKey = { 11, 12 };
	Assert(!SerializeDocument(wrongRemoteCacheKey).has_value());
	auto supportChanged = original;
	supportChanged.cacheTag++;
	const auto changedSupport = SerializeDocument(supportChanged);
	Assert(changedSupport.has_value());
	Assert(changedSupport->visible == serialized->visible);
	Assert(changedSupport->support != serialized->support);
	auto alternate = original;
	alternate.identity = { 21, 22 };
	alternate.cacheKey = { 21, 22 };
	const auto serializedAlternate = SerializeDocument(alternate);
	Assert(serializedAlternate.has_value());
	auto qualities = original;
	qualities.hasQualitiesList = true;
	qualities.qualities.push_back(*serializedAlternate);
	const auto serializedQualities = SerializeDocument(qualities);
	Assert(serializedQualities.has_value());
	Assert(serializedQualities->visible == serialized->visible);
	Assert(serializedQualities->support != serialized->support);
	checkRoundtrip(qualities);
	auto visibleChanged = original;
	visibleChanged.filename = u"edited.mp4"_q;
	const auto changedVisible = SerializeDocument(visibleChanged);
	Assert(changedVisible.has_value());
	Assert(changedVisible->visible != serialized->visible);
	auto voice = original;
	voice.type = VoiceDocument;
	voice.width = -1;
	voice.height = -1;
	voice.silentVideo = false;
	voice.coverVisible.clear();
	voice.coverSupport.clear();
	voice.videoCodec.clear();
	voice.videoPreloadPrefix = 0;
	voice.waveform = QByteArray(1, char(31));
	checkRoundtrip(voice);
	voice.waveform[0] = char(32);
	Assert(!SerializeDocument(voice).has_value());
	voice.waveform.resize(2);
	voice.waveform[0] = char(31);
	voice.waveform[1] = char(-1);
	Assert(!SerializeDocument(voice).has_value());
	auto sticker = original;
	sticker.type = StickerDocument;
	sticker.silentVideo = true;
	sticker.coverVisible.clear();
	sticker.coverSupport.clear();
	sticker.videoCodec.clear();
	sticker.videoPreloadPrefix = 0;
	sticker.skipPremiumEffect = true;
	sticker.sticker = StickerSnapshot{
		.type = StickerType::Webm,
		.setType = Data::StickersType::Stickers,
		.alt = u"🙂"_q,
		.premium = true,
		.textColor = true,
		.set = {
			.id = 5,
			.accessHash = 6,
		},
	};
	checkRoundtrip(sticker);
	auto invalidSticker = sticker;
	invalidSticker.videoPreloadPrefix = 2048;
	Assert(!SerializeDocument(invalidSticker).has_value());
	auto customEmoji = sticker;
	customEmoji.skipPremiumEffect = true;
	customEmoji.sticker->setType = Data::StickersType::Emoji;
	checkRoundtrip(customEmoji);
	auto song = original;
	song.type = SongDocument;
	song.silentVideo = false;
	song.coverVisible.clear();
	song.coverSupport.clear();
	song.videoCodec.clear();
	song.videoPreloadPrefix = 0;
	song.song = SongSnapshot{
		.title = u"Title"_q,
		.performer = u"Performer"_q,
	};
	checkRoundtrip(song);
	return true;
}

} // namespace

void ValidateDocumentMediaFormat() {
	static const auto checked = CheckDocumentMediaFormat();
	Q_UNUSED(checked);
}
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
