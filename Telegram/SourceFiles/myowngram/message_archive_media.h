// This file is part of MyOwnGram,
// a Telegram Desktop fork.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "myowngram/message_archive_record.h"

#include <optional>

namespace Data {
class Media;
} // namespace Data

namespace MyOwnGram::MessageArchiveStorage {

struct SerializedPhotoMedia {
	QByteArray visible;
	QByteArray support;
};

[[nodiscard]] std::optional<SerializedPhotoMedia> SerializePhotoMedia(
	const Data::Media *media);

#ifdef _DEBUG
void ValidatePhotoMediaFormat();
#endif // _DEBUG

} // namespace MyOwnGram::MessageArchiveStorage
