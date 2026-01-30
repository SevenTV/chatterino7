#include "providers/kick/KickMessageBuilder.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickBadges.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchBadge.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "util/BoostJsonWrap.hpp"
#include "util/FormatTime.hpp"
#include "util/Helpers.hpp"
#include "util/Variant.hpp"

namespace {

using namespace chatterino;
using namespace Qt::Literals;

EmotePtr lookupEmote(const KickChannel &channel, uint64_t senderID,
                     QStringView word)
{
    EmoteName wordStr(word.toString());  // FIXME: don't do this...
    const auto *globalFfzEmotes = getApp()->getFfzEmotes();
    const auto *globalBttvEmotes = getApp()->getBttvEmotes();
    const auto *globalSeventvEmotes = getApp()->getSeventvEmotes();

    EmotePtr emote;

    emote = getApp()->getSeventvPersonalEmotes()->getEmoteForKickUser(senderID,
                                                                      wordStr);
    if (emote)
    {
        return emote;
    }

    emote = channel.seventvEmote(wordStr);
    if (emote)
    {
        return emote;
    }

    emote = globalFfzEmotes->emote(wordStr).value_or(std::move(emote));
    if (emote)
    {
        return emote;
    }

    emote = globalBttvEmotes->emote(wordStr).value_or(std::move(emote));
    if (emote)
    {
        return emote;
    }

    emote =
        globalSeventvEmotes->globalEmote(wordStr).value_or(std::move(emote));
    if (emote)
    {
        return emote;
    }

    return emote;
}

void appendWord(KickMessageBuilder &builder, QStringView word)
{
    auto emote = lookupEmote(*builder.channel(), builder.senderID, word);
    if (emote)
    {
        builder.appendEmote(emote);
        return;
    }

    builder.addWordFromUserMessage(word, builder.channel());
}

bool isEmoteID(QStringView v)
{
    for (QChar c : v)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return !v.empty();
}

void appendNonKickEmoteText(KickMessageBuilder &builder, QStringView text)
{
    for (const auto &variant : getApp()->getEmotes()->getEmojis()->parse(text))
    {
        std::visit(variant::Overloaded{
                       [&](const EmotePtr &emote) {
                           builder.emplace<EmoteElement>(
                               emote, MessageElementFlag::EmojiAll);
                       },
                       [&](QStringView text) {
                           appendWord(builder, text);
                       },
                   },
                   variant);
    }
}

/// Try to find the next emote in `text`
///
/// Kick emotes are present as `[emote:{id}:{name}]` where `{id}` is numeric.
/// They can be right next to each other or to text. For example, we could find
/// the following message: `foo [emote:1234:name]foo[emote:1234:name]`.
bool tryAppendKickEmoteText(KickMessageBuilder &builder, QString &messageText,
                            QStringView &text)
{
    static constexpr QStringView emotePrefix = u"[emote:";

    auto nextEmote = text.indexOf(emotePrefix);
    if (nextEmote < 0)
    {
        return false;
    }
    auto secondColon = text.indexOf(u':', nextEmote + emotePrefix.size());
    if (secondColon < 0)
    {
        return false;
    }
    auto endBrace = text.indexOf(u']', secondColon + 1);
    if (endBrace < 0)
    {
        return false;
    }

    auto emoteID = text.sliced(nextEmote + emotePrefix.size(),
                               secondColon - nextEmote - emotePrefix.size());
    if (!isEmoteID(emoteID))
    {
        return false;
    }

    if (nextEmote > 0)
    {
        auto prefix = text.sliced(0, nextEmote);
        messageText.append(prefix);
        messageText.append(' ');
        appendNonKickEmoteText(builder, prefix);
    }

    auto emoteName = text.sliced(secondColon + 1, endBrace - secondColon - 1);
    builder.emplace<EmoteElement>(KickEmotes::emoteForID(emoteID, emoteName),
                                  MessageElementFlag::Emote,
                                  builder.textColor());
    messageText.append(emoteName);

    text = text.sliced(endBrace + 1);
    if (!text.empty())
    {
        messageText.append(' ');
    }

    return true;
}

void parseContent(KickMessageBuilder &builder, QString &messageText,
                  QStringView content)
{
    for (auto word : content.tokenize(u' ', Qt::SkipEmptyParts))
    {
        if (!messageText.isEmpty())
        {
            messageText.append(' ');
        }

        while (!word.empty())
        {
            if (!tryAppendKickEmoteText(builder, messageText, word))
            {
                messageText.append(word);
                appendNonKickEmoteText(builder, word);
                break;
            }
        }
    }
}

QString displayedUsername(const Message &message)
{
    QString usernameText;
    switch (getSettings()->usernameDisplayMode.getValue())
    {
        case UsernameDisplayMode::Username:
            usernameText = message.loginName;
            break;

        case UsernameDisplayMode::LocalizedName:
        case UsernameDisplayMode::UsernameAndLocalizedName:
        default:
            usernameText = message.displayName;
            break;
    }

    if (auto nicknameText = getSettings()->matchNickname(usernameText))
    {
        usernameText = *nicknameText;
    }

    return usernameText;
}

void checkThreadSubscription(const QString &senderLogin,
                             const QString &originalSender,
                             const std::shared_ptr<MessageThread> &thread)
{
    if (thread->subscribed() || thread->unsubscribed())
    {
        return;
    }

    if (getSettings()->autoSubToParticipatedThreads)
    {
        const auto &current = getApp()->getAccounts()->kick.current();
        if (current->isAnonymous())
        {
            return;
        }
        auto user = current->username();
        if (senderLogin.compare(user, Qt::CaseInsensitive) == 0 ||
            originalSender.compare(user, Qt::CaseInsensitive) == 0)
        {
            thread->markSubscribed();
        }
    }
}

// FIXME: this is 🍝
void appendReply(KickMessageBuilder &builder, BoostJsonObject metadata)
{
    auto originalMessage = metadata["original_message"].toObject();
    auto originalSender = metadata["original_sender"]["username"].toQString();
    auto originalMessageID = originalMessage["id"].toQString();
    auto [thread, replyParent] =
        builder.channel()->getOrCreateThread(originalMessageID);
    if (thread)
    {
        builder->replyThread = thread;
        builder->replyParent = replyParent;
        thread->addToThread(std::weak_ptr{builder.weakOf()});
        checkThreadSubscription(builder->loginName, originalSender, thread);
        if (thread->subscribed())
        {
            builder->flags.set(MessageFlag::SubscribedThread);
        }
    }

    builder->flags.set(MessageFlag::ReplyMessage);

    QString usernameText = originalSender;
    if (replyParent)
    {
        usernameText = displayedUsername(*replyParent);
    }

    builder.emplace<ReplyCurveElement>();

    auto *replyingTo = builder.emplace<TextElement>(
        "Replying to", MessageElementFlag::RepliedMessage, MessageColor::System,
        FontStyle::ChatMediumSmall);
    if (thread)
    {
        replyingTo->setLink({Link::ViewThread, thread->rootId()});
    }

    builder
        .emplace<TextElement>(
            '@' % usernameText % ':', MessageElementFlag::RepliedMessage,
            replyParent ? replyParent->usernameColor : QColor{},
            FontStyle::ChatMediumSmall)
        ->setLink({Link::UserInfo,
                   replyParent ? replyParent->displayName : usernameText});

    MessageColor color = MessageColor::Text;
    if (replyParent && replyParent->flags.has(MessageFlag::Action))
    {
        color = replyParent->usernameColor;
    }

    QString messageText = [&] {
        if (replyParent)
        {
            return replyParent->messageText;
        }
        return originalMessage["content"].toQString();
    }();

    auto *textEl = builder.emplace<SingleLineTextElement>(
        messageText,
        MessageElementFlags(
            {MessageElementFlag::RepliedMessage, MessageElementFlag::Text}),
        color, FontStyle::ChatMediumSmall);
    if (thread)
    {
        textEl->setLink({Link::ViewThread, thread->rootId()});
    }
}

void appendReplyButtons(KickMessageBuilder &builder)
{
    if (builder->replyThread)
    {
        auto &img = getResources().buttons.replyThreadDark;
        builder
            .emplace<CircularImageElement>(Image::fromResourcePixmap(img, 0.15),
                                           2, Qt::gray,
                                           MessageElementFlag::ReplyButton)
            ->setLink({Link::ViewThread, builder->replyThread->rootId()});
    }
    else
    {
        auto &img = getResources().buttons.replyDark;
        builder
            .emplace<CircularImageElement>(Image::fromResourcePixmap(img, 0.15),
                                           2, Qt::gray,
                                           MessageElementFlag::ReplyButton)
            ->setLink({Link::ReplyToMessage, builder->id});
    }
}

void appendKickBadges(KickMessageBuilder &builder, BoostJsonArray badges)
{
    bool hasMod = false;
    bool hasVip = false;
    for (auto badgeObj : badges)
    {
        auto ty = badgeObj["type"].toStringView();
        auto [emote, flag] = KickBadges::lookup(ty);
        if (!emote)
        {
            continue;
        }

        if (ty == "moderator")
        {
            hasMod = true;
        }
        else if (ty == "vip")
        {
            hasVip = true;
        }

        builder.emplace<BadgeElement>(emote, flag);
    }

    bool isSelf = builder->loginName ==
                  getApp()->getAccounts()->kick.current()->username();
    if (isSelf)
    {
        builder.channel()->setMod(hasMod);
        builder.channel()->setVip(hasVip);
    }
}

void appendSeventvBadge(KickMessageBuilder &builder)
{
    auto badge = getApp()->getSeventvBadges()->getKickBadge(builder.senderID);
    if (badge)
    {
        builder.emplace<BadgeElement>(*badge, MessageElementFlag::BadgeSevenTV);
    }
}

HighlightAlert processHighlights(KickMessageBuilder &builder,
                                 const MessageParseArgs &args)
{
    if (getSettings()->isBlacklistedUser(builder->loginName))
    {
        // Do nothing. We ignore highlights from this user.
        return {};
    }

    auto [highlighted, highlightResult] = getApp()->getHighlights()->check(
        args, {}, builder->loginName, builder->messageText, builder->flags,
        builder->platform);

    if (!highlighted)
    {
        return {};
    }

    // This message triggered one or more highlights, act upon the highlight result

    builder->flags.set(MessageFlag::Highlighted);
    builder->highlightColor = highlightResult.color;

    if (highlightResult.showInMentions)
    {
        builder->flags.set(MessageFlag::ShowInMentions);
    }

    return {
        .customSound = highlightResult.customSoundUrl.value_or(QUrl{}),
        .playSound = highlightResult.playSound,
        .windowAlert = highlightResult.alert,
    };
}

QStringView plural(QStringView pluralText, uint64_t n)
{
    assert(pluralText.endsWith('s'));
    if (n == 1)
    {
        return pluralText.sliced(0, pluralText.length() - 1);
    }
    return pluralText;
}

}  // namespace

namespace chatterino {

KickMessageBuilder::KickMessageBuilder(SystemMessageTag /* tag */,
                                       KickChannel *channel,
                                       const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().flags.set(MessageFlag::System);
    this->message().platform = MessagePlatform::Kick;
    this->message().serverReceivedTime = time;
}

KickMessageBuilder::KickMessageBuilder(KickChannel *channel,
                                       const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().platform = MessagePlatform::Kick;
    this->message().serverReceivedTime = time;
}

KickMessageBuilder::KickMessageBuilder(KickChannel *channel)
    : channel_(channel)
{
    this->message().platform = MessagePlatform::Kick;
}

std::pair<MessagePtrMut, HighlightAlert> KickMessageBuilder::makeChatMessage(
    KickChannel *kickChannel, BoostJsonObject data)
{
    auto id = data["id"].toQString();
    auto content = data["content"].toQString();
    auto createdAt = data["created_at"].toQString();

    auto sender = data["sender"].toObject();
    auto identity = sender["identity"].toObject();

    KickMessageBuilder builder(kickChannel);
    builder->channelName = kickChannel->getName();
    builder->id = id;
    builder->serverReceivedTime =
        QDateTime::fromString(createdAt, Qt::DateFormat::ISODate).toLocalTime();
    builder->parseTime = QTime::currentTime();
    builder->displayName = sender["username"].toQString();
    builder->loginName = builder->displayName.toLower();
    builder.senderID = sender["id"].toUint64();
    builder->userID = QString::number(builder.senderID);

    if (data["type"].toStringView() == "reply")
    {
        appendReply(builder, data["metadata"].toObject());
    }

    builder.appendChannelName();

    builder.emplace<TimestampElement>(builder->serverReceivedTime.time());
    builder.emplace<TwitchModerationElement>();

    appendKickBadges(builder, identity["badges"].toArray());
    appendSeventvBadge(builder);

    builder.appendUsername(identity);
    kickChannel->setUserColor(builder->displayName, builder->usernameColor);
    kickChannel->addRecentChatter(builder->displayName);

    QString messageText;
    parseContent(builder, messageText, content);
    appendReplyButtons(builder);

    builder->searchText =
        builder->loginName % ' ' % builder->displayName % u": " % messageText;
    builder->messageText = messageText;

    MessageParseArgs args;
    args.isStaffOrBroadcaster = kickChannel->isBroadcaster();

    auto highlightAlert = processHighlights(builder, args);

    return {builder.release(), highlightAlert};
}

MessagePtrMut KickMessageBuilder::makeTimeoutMessage(KickChannel *channel,
                                                     const QDateTime &now,
                                                     BoostJsonObject data)
{
    bool isPermanent = data["permanent"].toBool();
    auto duration = [&] {
        if (isPermanent)
        {
            return std::chrono::seconds{};
        }
        auto dur = data["duration"].toInt64();
        if (dur != 0)
        {
            return std::chrono::seconds{dur * 60};
        }
        auto expiresAt =
            QDateTime::fromString(data["expires_at"].toQString(), Qt::ISODate);
        if (!expiresAt.isValid())
        {
            return std::chrono::seconds{0};
        }
        return std::chrono::round<std::chrono::seconds>(expiresAt - now);
    }();

    KickMessageBuilder builder(systemMessage, channel, now);
    const auto timedOutUsername = data["user"]["username"].toQString();
    const auto moderatorUsername = data["banned_by"]["username"].toQString();

    bool hasBannedBy = !moderatorUsername.isEmpty();

    builder->flags.set(MessageFlag::PubSub, MessageFlag::Timeout,
                       MessageFlag::ModerationAction);

    QString text;

    if (hasBannedBy)
    {
        builder->loginName = moderatorUsername;
        builder.emplaceSystemTextAndUpdate(moderatorUsername, text)
            ->setLink({Link::UserInfo, moderatorUsername});
        if (isPermanent)
        {
            builder.emplaceSystemTextAndUpdate("permanently banned", text);
        }
        else
        {
            builder.emplaceSystemTextAndUpdate("timed out", text);
        }
        builder.emplaceSystemTextAndUpdate(timedOutUsername, text)
            ->setLink({Link::UserInfo, timedOutUsername});
    }
    else
    {
        builder.emplaceSystemTextAndUpdate(timedOutUsername, text)
            ->setLink({Link::UserInfo, timedOutUsername});
        if (isPermanent)
        {
            builder.emplaceSystemTextAndUpdate("has been permanently banned",
                                               text);
        }
        else
        {
            builder.emplaceSystemTextAndUpdate("has been timed out", text);
        }
    }

    if (!isPermanent)
    {
        builder.emplaceSystemTextAndUpdate("for", text);
        builder.emplaceSystemTextAndUpdate(
            formatTime(static_cast<int>(duration.count())), text);
    }

    builder->elements.back()->setTrailingSpace(false);
    text.removeLast();  // trailing space

    builder.emplaceSystemTextAndUpdate(".", text);
    builder->messageText = text;
    builder->searchText = text;
    builder->timeoutUser = timedOutUsername.toLower();

    return builder.release();
}

MessagePtrMut KickMessageBuilder::makeUntimeoutMessage(KickChannel *channel,
                                                       BoostJsonObject data)
{
    bool isPermanent = data["permanent"].toBool();

    KickMessageBuilder builder(systemMessage, channel,
                               QDateTime::currentDateTime());
    const auto timedOutUsername = data["user"]["username"].toQString();
    const auto moderatorUsername = data["unbanned_by"]["username"].toQString();

    bool hasBannedBy = !moderatorUsername.isEmpty();

    builder->flags.set(MessageFlag::PubSub, MessageFlag::Untimeout,
                       MessageFlag::ModerationAction);

    QString text;

    if (hasBannedBy)
    {
        builder->loginName = moderatorUsername;
        builder.appendMentionedUser(moderatorUsername, text);
        if (isPermanent)
        {
            builder.emplaceSystemTextAndUpdate("unbanned", text);
        }
        else
        {
            builder.emplaceSystemTextAndUpdate("untimed out", text);
        }
        builder.appendMentionedUser(timedOutUsername, text);
    }
    else
    {
        builder.appendMentionedUser(timedOutUsername, text);
        if (isPermanent)
        {
            builder.emplaceSystemTextAndUpdate("was unbanned", text);
        }
        else
        {
            builder.emplaceSystemTextAndUpdate("was untimed out", text);
        }
    }

    builder->elements.back()->setTrailingSpace(false);
    text.removeLast();  // trailing space

    builder.emplaceSystemTextAndUpdate(".", text);
    builder->messageText = text;
    builder->searchText = text;
    builder->timeoutUser = timedOutUsername;

    return builder.release();
}

MessagePtrMut KickMessageBuilder::makePinnedMessage(KickChannel *channel,
                                                    BoostJsonObject data)
{
    const auto creator = data["pinnedBy"]["username"].toQString();
    auto jsonMessage = data["message"];
    auto messageID = jsonMessage["id"].toQString();

    auto existing = channel->findMessageByID(messageID);
    auto messageText = [&] {
        if (existing)
        {
            return existing->messageText;
        }
        return jsonMessage["text"].toQString();
    }();
    auto limit = getSettings()->deletedMessageLengthLimit.getValue();
    if (limit > 0 && messageText.length() > limit)
    {
        messageText = QStringView(messageText).left(limit) % u'…';
    }

    KickMessageBuilder builder(systemMessage, channel,
                               QDateTime::currentDateTime());
    QString text;
    builder.appendMentionedUser(creator, text);
    builder.appendOrEmplaceSystemTextAndUpdate(u"pinned"_s, text);
    builder
        .emplace<TextElement>(messageText, MessageElementFlag::Text,
                              MessageColor::Text)
        ->setLink({Link::JumpToMessage, messageID});

    builder->searchText = text;
    builder->messageText = text;
    builder->loginName = creator.toLower();
    return builder.release();
}

MessagePtrMut KickMessageBuilder::makeHostMessage(KickChannel *channel,
                                                  BoostJsonObject data)
{
    const auto user = data["host_username"].toQString();
    const auto viewers = data["number_viewers"].toUint64();
    KickMessageBuilder builder(systemMessage, channel,
                               QDateTime::currentDateTime());
    QString text;
    builder.appendMentionedUser(user, text);
    builder.appendOrEmplaceSystemTextAndUpdate(
        u"hosted the stream with " % QString::number(viewers) % ' ' %
            plural(u"viewers", viewers) % '.',
        text);

    builder->searchText = text;
    builder->messageText = text;
    builder->loginName = user.toLower();
    return builder.release();
}

std::tuple<MessagePtrMut, MessagePtrMut, HighlightAlert>
    KickMessageBuilder::makeSubscriptionMessage(KickChannel *channel,
                                                BoostJsonObject data)
{
    MessagePtrMut customMessage;
    HighlightAlert alert;
    auto now = QDateTime::currentDateTime();

    auto months = data["months"].toUint64();
    auto username = data["username"].toQString();
    auto customMessageText = data["custom_message"].toQString();
    if (!customMessageText.isEmpty())
    {
        KickMessageBuilder builder(channel);
        builder->serverReceivedTime = now;
        builder->flags.set(MessageFlag::Subscription);
        builder->loginName = username;

        builder.emplace<TimestampElement>(now.time());
        builder.appendUsernameAsSender(username);
        QString text;
        parseContent(builder, text, customMessageText);

        builder->messageText = text;
        builder->searchText = username % ": " % text;

        MessageParseArgs args;
        args.isSubscriptionMessage = true;
        args.isStaffOrBroadcaster = channel->isBroadcaster();
        alert = processHighlights(builder, args);
        customMessage = builder.release();
    }

    KickMessageBuilder builder(systemMessage, channel, now);
    builder->flags.set(MessageFlag::Subscription);

    QString text;
    builder.appendMentionedUser(username, text);
    builder.appendOrEmplaceSystemTextAndUpdate(
        u"subscribed for " % QString::number(months) %
            plural(u" months", months) % '.',
        text);
    builder->searchText = text;
    builder->messageText = text;
    builder->loginName = username;

    return {customMessage, builder.release(), alert};
}

MessagePtrMut KickMessageBuilder::makeGiftedSubscriptionMessage(
    KickChannel *channel, BoostJsonObject data)
{
    const auto gifter = data["gifter_username"].toQString();
    const auto gifted = data["gifted_usernames"].toArray();
    auto total = data["gifter_total"].toUint64();

    if (gifted.empty())
    {
        return nullptr;
    }

    KickMessageBuilder builder(systemMessage, channel,
                               QDateTime::currentDateTime());
    builder->flags.set(MessageFlag::Subscription);

    QString text;
    builder.appendMentionedUser(gifter, text);
    builder.emplaceSystemTextAndUpdate(
        u"gifted " % QString::number(gifted.size()) %
            plural(u" subscriptions", gifted.size()) % u" to",
        text);

    for (size_t i = 0; i < gifted.size(); i++)
    {
        if (i != 0)
        {
            if (i == gifted.size() - 1)
            {
                QString toAdd = u", and"_s;
                if (gifted.size() == 2)
                {
                    toAdd = u"and"_s;
                    builder->elements.back()->setTrailingSpace(true);
                    text.append(' ');
                }
                builder.appendOrEmplaceSystemTextAndUpdate(toAdd, text);
            }
            else
            {
                builder.appendOrEmplaceSystemTextAndUpdate(u","_s, text);
            }
        }
        builder.appendMentionedUser(gifted[i].toQString(), text, false);
    }
    builder.appendOrEmplaceSystemTextAndUpdate(
        u". They gifted "_s % QString::number(total) % plural(u" subs", total) %
            u" in total.",
        text);

    builder->messageText = text;
    builder->searchText = text;
    builder->loginName = gifter;

    return builder.release();
}

MessagePtrMut KickMessageBuilder::makeRewardRedeemedMessage(
    KickChannel *channel, BoostJsonObject data)
{
    const auto reward = data["reward_title"].toQString();
    const auto username = data["username"].toQString();
    const auto userInput = data["user_input"].toQString();

    KickMessageBuilder builder(channel, QDateTime::currentDateTime());
    builder->flags.set(MessageFlag::RedeemedChannelPointReward);

    QString text;
    if (userInput.isEmpty())
    {
        builder.appendMentionedUser(username, text);
        builder.appendOrEmplaceText(u"redeemed"_s, MessageColor::Text);
        text += u" redeemed ";
    }
    else
    {
        builder.appendOrEmplaceText(u"Redeemed"_s, MessageColor::Text);
        text += u"Redeemed ";
    }
    builder.emplace<TextElement>(reward, MessageElementFlag::Text,
                                 MessageColor::Text, FontStyle::ChatMediumBold);
    text += reward;

    if (userInput.isEmpty())
    {
        builder->messageText = text;
        builder->searchText = text;
    }
    else
    {
        builder.emplace<LinebreakElement>(
            MessageElementFlag::ChannelPointReward);
        builder.appendUsernameAsSender(username);
        QString redeemedText;
        parseContent(builder, redeemedText, userInput);
        builder->messageText = text;
        builder->searchText = text % ' ' % username % u": " % redeemedText;
    }
    builder->loginName = username;
    return builder.release();
}

MessagePtrMut KickMessageBuilder::makeKicksGiftedMessage(KickChannel *channel,
                                                         BoostJsonObject data)
{
    const auto username = data["sender"]["username"].toQString();
    const auto gift = data["gift"].toObject();
    const auto giftName = gift["name"].toQString();
    const auto giftAmount = gift["amount"].toUint64();
    const auto userInput = data["message"].toQString();

    KickMessageBuilder builder(channel, QDateTime::currentDateTime());
    builder->flags.set(MessageFlag::RedeemedChannelPointReward);

    QString text;
    if (userInput.isEmpty())
    {
        builder.appendMentionedUser(username, text);
        builder.appendOrEmplaceText(u"gifted"_s, MessageColor::Text);
        text += u" gifted ";
    }
    else
    {
        builder.appendOrEmplaceText(u"Gifted"_s, MessageColor::Text);
        text += u"Gifted ";
    }
    builder.emplace<TextElement>(giftName, MessageElementFlag::Text,
                                 MessageColor::Text, FontStyle::ChatMediumBold);
    QString kickInfo =
        '(' % localizeNumbers(giftAmount) % plural(u" Kicks", giftAmount) % ')';
    builder.appendOrEmplaceText(kickInfo, MessageColor::Text);
    text += kickInfo;

    if (userInput.isEmpty())
    {
        builder->messageText = text;
        builder->searchText = text;
    }
    else
    {
        builder.emplace<LinebreakElement>(
            MessageElementFlag::ChannelPointReward);
        builder.appendUsernameAsSender(username);
        QString redeemedText;
        parseContent(builder, redeemedText, userInput);
        builder->messageText = text;
        builder->searchText = text % ' ' % username % u": " % redeemedText;
    }
    builder->loginName = username;
    return builder.release();
}

MessagePtrMut KickMessageBuilder::makeRoomModeMessage(
    KickChannel *channel, const QString &mode, bool enabled,
    std::optional<std::chrono::seconds> duration)
{
    KickMessageBuilder builder(systemMessage, channel,
                               QDateTime::currentDateTime());
    builder->flags.set(MessageFlag::ModerationAction);
    QString text;

    QString enabledText = enabled ? u"enabled"_s : u"disabled"_s;

    builder.emplaceSystemTextAndUpdate(mode, text);
    builder.emplaceSystemTextAndUpdate(u"mode"_s, text);

    if (duration && duration->count() > 0)
    {
        builder.emplaceSystemTextAndUpdate(u'(' % formatTime(*duration) % u')',
                                           text);
    }

    builder.emplaceSystemTextAndUpdate(u"has been"_s, text);
    builder.emplaceSystemTextAndUpdate(enabledText, text);

    text.removeLast();
    builder->elements.back()->setTrailingSpace(false);
    builder.emplaceSystemTextAndUpdate(u"."_s, text);

    builder->messageText = text;
    builder->searchText = text;
    return builder.release();
}

void KickMessageBuilder::appendChannelName()
{
    QString channelName('#' + this->channel()->getName());
    Link link(Link::JumpToChannel, u":kick:" % this->channel()->getName());

    this->emplace<TextElement>(channelName, MessageElementFlag::ChannelName,
                               MessageColor::System)
        ->setLink(link);
}

void KickMessageBuilder::appendUsername(BoostJsonObject identityObj)
{
    QString usernameText = displayedUsername(this->message()) + ':';

    auto userColor = QColor::fromString(identityObj["color"].toStringView());
    this->message().usernameColor = userColor;
    this->emplace<TextElement>(usernameText,
                               MessageElementFlags{
                                   MessageElementFlag::Username,
                                   MessageElementFlag::KickUsername,
                               },
                               userColor, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, this->message().displayName});
}

void KickMessageBuilder::appendUsernameAsSender(const QString &username)
{
    auto userColor = this->channel()->getUserColor(username);
    MessageColor userMessageColor(userColor);
    if (!userColor.isValid())
    {
        userMessageColor = MessageColor::Text;
    }
    this->emplace<TextElement>(username + ':',
                               MessageElementFlags{
                                   MessageElementFlag::Username,
                                   MessageElementFlag::KickUsername,
                               },
                               userMessageColor, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, username});
}

void KickMessageBuilder::appendMentionedUser(const QString &username,
                                             QString &text, bool trailingSpace)
{
    auto *el =
        this->emplace<MentionElement>(username, username, MessageColor::System,
                                      this->channel()->getUserColor(username));
    el->addFlags(MessageElementFlag::KickUsername);
    text.append(username);

    if (trailingSpace)
    {
        text.append(u' ');
    }
    else
    {
        el->setTrailingSpace(false);
    }
}

}  // namespace chatterino
