/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "myowngram/story_action_permission.h"

#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"

namespace MyOwnGram {
namespace {

namespace ActivityReporting = ::MyOwnGram::ActivityReporting;
using StoryAction = ActivityReporting::StoryAction;

QString ActionName(StoryAction action) {
	switch (action) {
	case StoryAction::View:
		Unexpected("Interactive story action expected.");
	case StoryAction::Reaction:
		return tr::lng_myowngram_story_reactions(tr::now);
	case StoryAction::Reply:
		return tr::lng_myowngram_story_replies(tr::now);
	case StoryAction::Share:
		return tr::lng_myowngram_story_shares(tr::now);
	case StoryAction::Link:
		return tr::lng_myowngram_story_links(tr::now);
	case StoryAction::LiveJoin:
		return tr::lng_myowngram_live_story_joins(tr::now);
	case StoryAction::LiveReaction:
		return tr::lng_myowngram_live_story_reactions(tr::now);
	case StoryAction::LiveComment:
		return tr::lng_myowngram_live_story_comments(tr::now);
	}
	Unexpected("StoryAction value.");
}

rpl::producer<QString> PromptTitle(StoryAction action) {
	switch (action) {
	case StoryAction::View:
		Unexpected("Interactive story action expected.");
	case StoryAction::Reaction:
		return tr::lng_myowngram_story_reaction_prompt_title();
	case StoryAction::Reply:
		return tr::lng_myowngram_story_reply_prompt_title();
	case StoryAction::Share:
		return tr::lng_myowngram_story_share_prompt_title();
	case StoryAction::Link:
		return tr::lng_myowngram_story_link_prompt_title();
	case StoryAction::LiveJoin:
		return tr::lng_myowngram_live_story_prompt_title();
	case StoryAction::LiveReaction:
		return tr::lng_myowngram_live_story_reaction_prompt_title();
	case StoryAction::LiveComment:
		return tr::lng_myowngram_live_story_comment_prompt_title();
	}
	Unexpected("StoryAction value.");
}

rpl::producer<TextWithEntities> PromptText(
		StoryAction action,
		const QString &name) {
	auto markedName = rpl::single(tr::bold(name));
	switch (action) {
	case StoryAction::View:
		Unexpected("Interactive story action expected.");
	case StoryAction::Reaction:
		return tr::lng_myowngram_story_reaction_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::Reply:
		return tr::lng_myowngram_story_reply_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::Share:
		return tr::lng_myowngram_story_share_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::Link:
		return tr::lng_myowngram_story_link_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::LiveJoin:
		return tr::lng_myowngram_live_story_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::LiveReaction:
		return tr::lng_myowngram_live_story_reaction_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	case StoryAction::LiveComment:
		return tr::lng_myowngram_live_story_comment_prompt(
			lt_name,
			std::move(markedName),
			tr::marked);
	}
	Unexpected("StoryAction value.");
}

rpl::producer<QString> ConfirmText(StoryAction action) {
	switch (action) {
	case StoryAction::View:
		Unexpected("Interactive story action expected.");
	case StoryAction::Reaction:
		return tr::lng_myowngram_story_reaction_prompt_allow();
	case StoryAction::Reply:
		return tr::lng_myowngram_story_reply_prompt_allow();
	case StoryAction::Share:
		return tr::lng_myowngram_story_share_prompt_allow();
	case StoryAction::Link:
		return tr::lng_myowngram_story_link_prompt_allow();
	case StoryAction::LiveJoin:
		return tr::lng_myowngram_live_story_prompt_allow();
	case StoryAction::LiveReaction:
		return tr::lng_myowngram_story_reaction_prompt_allow();
	case StoryAction::LiveComment:
		return tr::lng_myowngram_live_story_comment_prompt_allow();
	}
	Unexpected("StoryAction value.");
}

} // namespace

void RequestInteractiveStoryAction(
		std::shared_ptr<ChatHelpers::Show> show,
		StoryAction action,
		not_null<PeerData*> peer,
		Fn<void()> allowed,
		Fn<void()> denied,
		bool viewerStyle) {
	Expects(action != StoryAction::View);

	using Policy = ActivityReporting::StoryActionPolicy;
	const auto policy = ActivityReporting::StoryPolicy(action);
	if (policy == Policy::Allow) {
		allowed();
		return;
	} else if (policy == Policy::Block) {
		show->showToast(tr::lng_myowngram_story_action_blocked(
			tr::now,
			lt_action,
			ActionName(action)));
		if (denied) {
			denied();
		}
		return;
	}

	const auto deniedCallback = std::make_shared<Fn<void()>>(
		std::move(denied));
	struct State {
		Ui::Checkbox *remember = nullptr;
		bool resolved = false;
	};
	const auto state = std::make_shared<State>();
	show->show(Box([=, allowed = std::move(allowed)](
			not_null<Ui::GenericBox*> box) mutable {
		const auto deny = [=] {
			auto callback = std::move(*deniedCallback);
			if (callback) {
				callback();
			}
		};
		Ui::ConfirmBox(box, {
			.text = PromptText(action, peer->shortName()),
			.confirmed = [=, allowed = std::move(allowed)](
					Fn<void()> close) mutable {
				state->resolved = true;
				const auto blocked = (ActivityReporting::StoryPolicy(action)
					== Policy::Block);
				if (!blocked && state->remember->checked()) {
					ActivityReporting::SetStoryPolicy(
						action,
						Policy::Allow);
				}
				auto callback = blocked
					? std::move(*deniedCallback)
					: std::move(allowed);
				close();
				if (blocked) {
					show->showToast(
						tr::lng_myowngram_story_action_blocked(
							tr::now,
							lt_action,
							ActionName(action)));
				}
				if (callback) {
					callback();
				}
			},
			.cancelled = [=](Fn<void()> close) {
				state->resolved = true;
				if (state->remember->checked()) {
					ActivityReporting::SetStoryPolicy(
						action,
						Policy::Block);
				}
				close();
				deny();
			},
			.confirmText = ConfirmText(action),
			.cancelText = tr::lng_box_no(),
			.labelStyle = viewerStyle ? &st::storiesBoxLabel : nullptr,
			.title = PromptTitle(action),
			.strictCancel = true,
		});
		const auto &checkboxStyle = viewerStyle
			? st::storiesComposeControls.files.checkbox
			: st::defaultBoxCheckbox;
		const auto &checkStyle = viewerStyle
			? st::storiesComposeControls.files.check
			: st::defaultCheck;
		auto padding = st::boxPadding;
		padding.setTop(padding.bottom());
		state->remember = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_myowngram_story_prompt_remember(tr::now),
				false,
				checkboxStyle,
				checkStyle),
			std::move(padding));
		box->boxClosing() | rpl::on_next([=] {
			if (state->resolved) {
				return;
			}
			state->resolved = true;
			deny();
		}, box->lifetime());
	}));
}

} // namespace MyOwnGram
