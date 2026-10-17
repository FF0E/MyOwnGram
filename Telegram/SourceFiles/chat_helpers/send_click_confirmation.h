/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <crl/crl_time.h>

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace ChatHelpers {

class Show;

class SendClickConfirmation final {
public:
	enum class Type {
		Sticker,
		Gif,
	};

	~SendClickConfirmation();

	[[nodiscard]] bool confirm(
		not_null<Show*> show,
		const void *item,
		Type type = Type::Sticker);
	void clear();

private:
	const void *_item = nullptr;
	crl::time _until = 0;
	base::weak_ptr<Ui::Toast::Instance> _toast;

};

} // namespace ChatHelpers
