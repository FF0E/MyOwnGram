// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#include "myowngram/message_archive_media.h"

#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "ui/image/image_location.h"

#include <QtCore/QDataStream>

namespace MyOwnGram::MessageArchiveStorage {
namespace {

constexpr auto kPhotoVisibleVersion = uint16(1);
constexpr auto kPhotoSupportVersion = uint16(1);
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

} // namespace

std::optional<SerializedPhotoMedia> SerializePhotoMedia(
		const Data::Media *media) {
	const auto photo = (media && !media->webpage())
		? media->photo()
		: nullptr;
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
	const auto snapshot = PhotoSnapshot{
		.id = extendedPreview ? PhotoId(0) : photo->id,
		.spoiler = media->hasSpoiler(),
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
	};
	return SerializePhoto(snapshot);
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
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
