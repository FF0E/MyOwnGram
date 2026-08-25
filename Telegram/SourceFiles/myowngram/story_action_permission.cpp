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
	show->show(Ui::MakeConfirmBox({
		.text = PromptText(action, peer->shortName()),
		.confirmed = [=, allowed = std::move(allowed)](
				Fn<void()> close) mutable {
			const auto blocked = (ActivityReporting::StoryPolicy(action)
				== Policy::Block);
			auto callback = blocked
				? std::move(*deniedCallback)
				: std::move(allowed);
			close();
			if (blocked) {
				show->showToast(tr::lng_myowngram_story_action_blocked(
					tr::now,
					lt_action,
					ActionName(action)));
			}
			if (callback) {
				callback();
			}
		},
		.cancelled = [=](Fn<void()> close) mutable {
			auto callback = std::move(*deniedCallback);
			close();
			if (callback) {
				callback();
			}
		},
		.confirmText = ConfirmText(action),
		.cancelText = tr::lng_cancel(),
		.labelStyle = viewerStyle ? &st::storiesBoxLabel : nullptr,
		.title = PromptTitle(action),
	}));
}

} // namespace MyOwnGram
