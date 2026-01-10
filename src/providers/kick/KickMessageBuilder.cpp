#include "providers/kick/KickMessageBuilder.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/kick/KickBadges.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "util/BoostJsonWrap.hpp"
#include "util/Variant.hpp"

namespace {

using namespace chatterino;

EmotePtr lookupEmote(const KickChannel &channel, QStringView word)
{
    EmoteName wordStr(word.toString());  // FIXME: don't do this...
    const auto *globalFfzEmotes = getApp()->getFfzEmotes();
    const auto *globalBttvEmotes = getApp()->getBttvEmotes();
    const auto *globalSeventvEmotes = getApp()->getSeventvEmotes();

    EmotePtr emote;

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

void appendWord(MessageBuilder &builder, KickChannel &channel, QStringView word)
{
    auto emote = lookupEmote(channel, word);
    if (emote)
    {
        builder.appendEmote(emote);
        return;
    }

    builder.addWordFromUserMessage(word, &channel);
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

void appendNonKickEmoteText(MessageBuilder &builder, KickChannel &channel,
                            QStringView text)
{
    for (const auto &variant : getApp()->getEmotes()->getEmojis()->parse(text))
    {
        std::visit(variant::Overloaded{
                       [&](const EmotePtr &emote) {
                           builder.emplace<EmoteElement>(
                               emote, MessageElementFlag::EmojiAll);
                       },
                       [&](QStringView text) {
                           appendWord(builder, channel, text);
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
bool tryAppendKickEmoteText(MessageBuilder &builder, KickChannel &channel,
                            QString &messageText, QStringView &text)
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
        appendNonKickEmoteText(builder, channel, prefix);
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

void parseContent(MessageBuilder &builder, KickChannel &channel,
                  QString &messageText, QStringView content)
{
    for (auto word : content.tokenize(u' ', Qt::SkipEmptyParts))
    {
        if (!messageText.isEmpty())
        {
            messageText.append(' ');
        }

        while (!word.empty())
        {
            if (!tryAppendKickEmoteText(builder, channel, messageText, word))
            {
                messageText.append(word);
                appendNonKickEmoteText(builder, channel, word);
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
        // FIXME: use kick account
        const auto &currentLogin =
            getApp()->getAccounts()->twitch.getCurrent()->getUserName();

        if (senderLogin == currentLogin || originalSender == currentLogin)
        {
            thread->markSubscribed();
        }
    }
}

// FIXME: this is 🍝
void appendReply(MessageBuilder &builder, KickChannel *channel,
                 BoostJsonObject metadata)
{
    auto originalMessage = metadata["original_message"].toObject();
    auto originalSender = metadata["original_sender"]["username"].toQString();
    auto originalMessageID = originalMessage["id"].toQString();
    auto thread = channel->getOrCreateThread(originalMessageID);
    MessagePtr threadRoot;
    if (thread)
    {
        builder->replyThread = thread;
        // there's no replyParent like on Twitch
        threadRoot = thread->root();
        thread->addToThread(std::weak_ptr{builder.weakOf()});
        checkThreadSubscription(builder->loginName, originalSender, thread);
        if (thread->subscribed())
        {
            builder->flags.set(MessageFlag::SubscribedThread);
        }
    }

    builder->flags.set(MessageFlag::ReplyMessage);

    QString usernameText = originalSender;
    if (threadRoot)
    {
        usernameText = displayedUsername(*threadRoot);
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
        .emplace<TextElement>('@' % usernameText % ':',
                              MessageElementFlag::RepliedMessage,
                              threadRoot ? threadRoot->usernameColor : QColor{},
                              FontStyle::ChatMediumSmall)
        ->setLink({Link::UserInfo,
                   threadRoot ? threadRoot->displayName : usernameText});

    MessageColor color = MessageColor::Text;
    if (threadRoot && threadRoot->flags.has(MessageFlag::Action))
    {
        color = threadRoot->usernameColor;
    }

    QString messageText = [&] {
        if (threadRoot)
        {
            return threadRoot->messageText;
        }
        return originalMessage["content"].toQString();
    }();

    auto *textEl = builder.emplace<SingleLineTextElement>(
        messageText,
        MessageElementFlags(
            {MessageElementFlag::RepliedMessage, MessageElementFlag::Text}),
        color, FontStyle::ChatMediumSmall);
    if (threadRoot)
    {
        textEl->setLink({Link::ViewThread, thread->rootId()});
    }
}

void appendChannelName(MessageBuilder &builder, const Channel *channel)
{
    QString channelName('#' + channel->getName());
    Link link(Link::JumpToChannel, u":kick:" % channel->getName());

    builder
        .emplace<TextElement>(channelName, MessageElementFlag::ChannelName,
                              MessageColor::System)
        ->setLink(link);
}

void appendUsername(MessageBuilder &builder, BoostJsonObject senderObj,
                    BoostJsonObject identityObj)
{
    builder->displayName = senderObj["username"].toQString();

    QString usernameText = displayedUsername(builder.message()) + ':';

    auto userColor = QColor::fromString(identityObj["color"].toStringView());
    builder->usernameColor = userColor;
    builder
        .emplace<TextElement>(usernameText, MessageElementFlag::Username,
                              userColor, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, builder.message().displayName});
}

void appendKickBadges(MessageBuilder &builder, BoostJsonArray badges)
{
    for (auto badgeObj : badges)
    {
        auto ty = badgeObj["type"].toStringView();
        auto [emote, flag] = KickBadges::lookup(ty);
        if (!emote)
        {
            continue;
        }
        builder.emplace<BadgeElement>(emote, flag);
    }
}

}  // namespace

namespace chatterino {

MessagePtrMut KickMessageBuilder::makeChatMessage(KickChannel *kickChannel,
                                                  BoostJsonObject data)
{
    auto id = data["id"].toQString();
    auto content = data["content"].toQString();
    auto createdAt = data["created_at"].toQString();

    auto sender = data["sender"].toObject();
    auto identity = sender["identity"].toObject();

    MessageBuilder builder;
    builder->channelName = kickChannel->getName();
    builder->id = id;
    builder->serverReceivedTime =
        QDateTime::fromString(createdAt, Qt::DateFormat::ISODate);
    builder->parseTime = QTime::currentTime();
    builder->loginName = sender["slug"].toQString();
    builder->userID = QString::number(sender["id"].toUint64());

    if (data["type"].toStringView() == "reply")
    {
        appendReply(builder, kickChannel, data["metadata"].toObject());
    }

    appendChannelName(builder, kickChannel);

    builder.emplace<TimestampElement>(builder->serverReceivedTime.time());
    builder.emplace<TwitchModerationElement>();

    appendKickBadges(builder, identity["badges"].toArray());
    // FIXME: append seventv badges

    appendUsername(builder, sender, identity);
    kickChannel->setUserColor(builder->displayName, builder->usernameColor);
    kickChannel->addRecentChatter(builder->displayName);

    QString messageText;
    parseContent(builder, *kickChannel, messageText, content);

    builder->searchText =
        builder->loginName % ' ' % builder->displayName % u": " % messageText;
    builder->messageText = messageText;

    return builder.release();
}

}  // namespace chatterino
