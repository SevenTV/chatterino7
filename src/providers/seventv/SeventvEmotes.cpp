#include "providers/seventv/SeventvEmotes.hpp"

#include "SeventvEmoteCache.hpp"
#include "common/Common.hpp"
#include "common/NetworkRequest.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <utility>

namespace chatterino {
namespace {
    const QRegularExpression whitespaceRegex(R"(\s+)");

    const QString CHANNEL_HAS_NO_EMOTES(
        "This channel has no 7TV channel emotes.");
    const QString emoteLinkFormat("https://7tv.app/emotes/%1");
    const QString apiUrlChannelEmotes("https://7tv.io/v3/users/twitch/%1");

    const QString beforeBaseName("<br>Alias to ");
    const QString beforeCreator("7TV Emote<br>By: ");
    const QString breakTag("<br>");

    // maximum pageSize that 7tv's API accepts
    constexpr int maxPageSize = 150;

    static std::unordered_map<EmoteId, std::weak_ptr<const Emote>> emoteCache;
    static std::mutex emoteCacheMutex;
    static SeventvEmoteCache v3Cache;
    static std::mutex v3CacheMutex;

    Url getEmoteLink(const EmoteId &id, const QString &emoteScale)

    {
        const QString urlTemplate("https://cdn.7tv.app/emote/%1/%2");

        return {urlTemplate.arg(id.string, emoteScale)};
    }

    EmotePtr cachedOrMake(Emote &&emote, const EmoteId &id)
    {
        return cachedOrMakeEmotePtr(std::move(emote), emoteCache,
                                    emoteCacheMutex, id);
    }

    struct CreateEmoteResult {
        EmoteId id;
        EmoteName name;
        Emote emote;
    };

    CreateEmoteResult createEmote(const QJsonValue &jsonEmote, bool isGlobal)
    {
        auto id = EmoteId{jsonEmote.toObject().value("id").toString()};
        auto name = EmoteName{jsonEmote.toObject().value("name").toString()};
        auto author = EmoteAuthor{jsonEmote.toObject()
                                      .value("owner")
                                      .toObject()
                                      .value("display_name")
                                      .toString()};
        int64_t visibility = jsonEmote.toObject().value("visibility").toInt();
        auto visibilityFlags =
            SeventvEmoteVisibilityFlags(SeventvEmoteVisibilityFlag(visibility));
        bool zeroWidth =
            visibilityFlags.has(SeventvEmoteVisibilityFlag::ZeroWidth);

        auto heightArr = jsonEmote.toObject().value("height").toArray();

        auto size1x = heightArr.at(0).toDouble();
        auto size2x = size1x * 2;
        auto size3x = size1x * 3;
        auto size4x = size1x * 4;

        if (heightArr.size() >= 2)
        {
            size2x = heightArr.at(1).toDouble();
        }
        if (heightArr.size() >= 3)
        {
            size3x = heightArr.at(2).toDouble();
        }
        if (heightArr.size() >= 4)
        {
            size4x = heightArr.at(3).toDouble();
        }

        auto emote = Emote(
            {name,
             ImageSet{Image::fromUrl(getEmoteLink(id, "1x"), size1x / size1x),
                      Image::fromUrl(getEmoteLink(id, "2x"), size1x / size2x),
                      Image::fromUrl(getEmoteLink(id, "3x"), size1x / size3x),
                      Image::fromUrl(getEmoteLink(id, "4x"), size1x / size4x)},
             Tooltip{QString("%1<br>%2 7TV Emote<br>By: %3")
                         .arg(name.string, (isGlobal ? "Global" : "Channel"),
                              author.string)},
             Url{emoteLinkFormat.arg(id.string)}, zeroWidth});

        auto result = CreateEmoteResult({id, name, emote});
        return result;
    }

    bool isZeroWidthActive(const QJsonObject &addedEmote)
    {
        auto flags = SeventvActiveEmoteFlags(
            SeventvActiveEmoteFlag(addedEmote.value("flags").toInt()));
        return flags.has(SeventvActiveEmoteFlag::ZeroWidth);
    }

    bool isZeroWidthRecommended(const QJsonObject &emoteData)
    {
        auto flags = SeventvEmoteFlags(
            SeventvEmoteFlag(emoteData.value("flags").toInt()));
        return flags.has(SeventvEmoteFlag::ZeroWidth);
    }

    ImageSet makeImageSet(const QJsonObject &jsonEmote)
    {
        auto host = jsonEmote["host"].toObject();
        auto baseUrl = host["url"].toString();
        auto files = host["files"].toArray();

        std::array<ImagePtr, 4> sizes;
        double baseWidth = 0.0;
        int nextSize = 0;

        for (auto fileItem : files)
        {
            if (nextSize >= sizes.size())
            {
                break;
            }

            auto file = fileItem.toObject();
            if (file["format"].toString() != "WEBP")
            {
                continue;
            }

            double width = file["width"].toDouble();
            double scale = 1.0;
            if (baseWidth > 0.0)
            {
                scale = baseWidth / width;
            }
            else
            {
                baseWidth = width;
            }

            auto image = Image::fromUrl(
                {QString("https:%1/%2").arg(baseUrl, file["name"].toString())},
                scale);

            sizes[nextSize] = image;
            nextSize++;
        }

        if (nextSize < sizes.size())
        {  // this should be really rare
            if (nextSize == 0)
            {
                qCWarning(chatterinoSeventv)
                    << "Got file list without any eligible files";
                return ImageSet{};
            }
            for (; nextSize < sizes.size(); nextSize++)
            {
                sizes[nextSize] = Image::getEmpty();
            }
        }

        return ImageSet{};
    }

    Tooltip createTooltip(const QString &name, const QString &author,
                          bool isGlobal)
    {
        // When changing, make sure to update createAliasedTooltip,
        // beforeCreator, beforeBaseName, and breakTag as well.
        return Tooltip{QString("%1<br>%2 7TV Emote<br>By: %3")
                           .arg(name, isGlobal ? "Global" : "Channel",
                                author.isEmpty() ? "<deleted>" : author)};
    }

    Tooltip createAliasedTooltip(const QString &name, const QString &baseName,
                                 const QString &author, bool isGlobal)
    {
        return Tooltip{QString("%1<br>Alias to %2<br>%3 7TV Emote<br>By: %4")
                           .arg(name, baseName, isGlobal ? "Global" : "Channel",
                                author.isEmpty() ? "<deleted>" : author)};
    }

    /**
     * @return (imageSet, fromCache)
     */
    std::pair<ImageSet, bool> lockOrCreateImageSet(const QJsonObject &emoteData,
                                                   WeakImageSet *cached)
    {
        if (cached)
        {
            if (auto locked = cached->lock())
            {
                return std::make_pair(locked.get(), true);
            }
        }
        return std::make_pair(makeImageSet(emoteData), false);
    }

    /**
     * Creates a "regular" (i.e. not aliased or global) emote
     * that will be added to the cache.
     */
    Emote createBaseEmoteV3(const EmoteId &id, const QJsonObject &emoteData,
                            WeakImageSet *cached)
    {
        auto name = EmoteName{emoteData.value("name").toString()};
        auto author = EmoteAuthor{emoteData.value("owner")
                                      .toObject()
                                      .value("display_name")
                                      .toString()};
        bool zeroWidth = isZeroWidthRecommended(emoteData);
        // This isn't cached since the entire emote will be cached
        auto imageSet = lockOrCreateImageSet(emoteData, cached).first;

        auto emote = Emote({name, imageSet,
                            createTooltip(name.string, author.string, false),
                            Url{emoteLinkFormat.arg(id.string)}, zeroWidth});

        return emote;
    }

    /**
     * Creates a new aliased or global emote where the base
     * emote isn't cached. The emote's images may be cached
     * already (supplied by <code>imageSet</code>.
     */
    Emote createV3AliasedOrGlobalEmote(const EmoteId &id, bool isGlobal,
                                       const QJsonObject &addedEmote,
                                       const QJsonObject &emoteData,
                                       WeakImageSet *cachedImages)
    {
        auto name = EmoteName{addedEmote.value("name").toString()};
        auto author = EmoteAuthor{emoteData.value("owner")
                                      .toObject()
                                      .value("display_name")
                                      .toString()};
        bool zeroWidth = isZeroWidthActive(addedEmote);
        bool aliasedName =
            addedEmote["name"].toString() != emoteData["name"].toString();
        auto tooltip = aliasedName
                           ? createAliasedTooltip(name.string,
                                                  emoteData["name"].toString(),
                                                  author.string, false)
                           : createTooltip(name.string, author.string, false);
        auto imageSet = lockOrCreateImageSet(emoteData, cachedImages);
        if (!imageSet.second)
        {
            v3Cache.putImageSet(id, imageSet.first);
        }

        auto emote = Emote({name, imageSet.first, tooltip,
                            Url{emoteLinkFormat.arg(id.string)}, zeroWidth});

        return emote;
    }

    /**
     * Creates an aliased or global emote where
     * the base emote is cached.
     */
    Emote forkExistingEmote(const QJsonObject &addedEmote,
                            const QJsonObject &emoteData,
                            const EmotePtr &baseEmote, bool isGlobal)
    {
        auto name = EmoteName{addedEmote.value("name").toString()};
        auto author = emoteData["owner"].toObject()["display_name"].toString();
        bool isAliased =
            addedEmote["name"].toString() != baseEmote->name.string;
        auto tooltip = isAliased ? createAliasedTooltip(name.string,
                                                        baseEmote->name.string,
                                                        author, isGlobal)
                                 : createTooltip(name.string, author, isGlobal);
        bool zeroWidth = isZeroWidthActive(addedEmote);

        auto emote = Emote(
            {name, baseEmote->images, tooltip, baseEmote->homePage, zeroWidth});
        return emote;
    }

    std::pair<Outcome, EmoteMap> parseGlobalEmotes(
        const QJsonArray &jsonEmotes, const EmoteMap &currentEmotes)
    {
        auto emotes = EmoteMap();

        // We always show all global emotes, no need to check visibility here
        for (const auto &jsonEmote : jsonEmotes)
        {
            auto emote = createEmote(jsonEmote, true);
            emotes[emote.name] =
                cachedOrMakeEmotePtr(std::move(emote.emote), currentEmotes);
        }

        return {Success, std::move(emotes)};
    }

    bool checkEmoteVisibilityV3(const QJsonObject &emoteData)
    {
        if (!emoteData.value("listed").toBool())
        {
            return getSettings()->showUnlistedEmotes;
        }
        auto flags = SeventvEmoteFlags(
            SeventvEmoteFlag(emoteData.value("flags").toInt()));
        return !flags.has(SeventvEmoteFlag::ContentTwitchDisallowed);
    }

    EmotePtr createEmoteV3(const QJsonObject &addedEmote,
                           const QJsonObject &emoteData, bool isGlobal)
    {
        auto emoteId = EmoteId{addedEmote["id"].toString()};
        bool isAliased =
            addedEmote["name"].toString() != emoteData["name"].toString() ||
            isZeroWidthActive(addedEmote) != isZeroWidthRecommended(emoteData);
        bool isCacheable = !isAliased && !isGlobal;

        if (auto cached = v3Cache.getEmote(emoteId))
        {
            if (isCacheable)
            {
                return cached;
            }
            return std::make_shared<const Emote>(
                forkExistingEmote(addedEmote, emoteData, cached, isGlobal));
        }

        auto *cachedImages = v3Cache.getImageSet(emoteId);
        if (isCacheable)
        {
            // Cache the entire emote
            return v3Cache.putEmote(
                emoteId, createBaseEmoteV3(emoteId, emoteData, cachedImages));
        }

        auto emote = std::make_shared<const Emote>(createV3AliasedOrGlobalEmote(
            emoteId, isGlobal, addedEmote, emoteData, cachedImages));
        if (cachedImages == nullptr)
        {
            // Only cache the images, not the entire emote
            v3Cache.putImageSet(emoteId, emote->images);
        }
        return emote;
    }

    EmoteMap parseChannelEmotes(const QJsonArray &jsonEmotes)
    {
        auto emotes = EmoteMap();
        std::lock_guard<std::mutex> guard(v3CacheMutex);

        for (auto jsonEmote_ : jsonEmotes)
        {
            auto addedEmote = jsonEmote_.toObject();
            auto emoteData = addedEmote["data"].toObject();

            if (!checkEmoteVisibilityV3(emoteData))
            {
                continue;
            }
            auto emote = createEmoteV3(addedEmote, emoteData, false);
            emotes[emote->name] = emote;
        }

        return emotes;
    }

    void updateEmoteMapPtr(Atomic<std::shared_ptr<const EmoteMap>> &map,
                           EmoteMap &&updatedMap)
    {
        map.set(std::make_shared<EmoteMap>(std::move(updatedMap)));
    }

    QString extractCreator(const Tooltip &tooltip)
    {
        int index = tooltip.string.indexOf(beforeCreator);
        return tooltip.string.mid(index + beforeCreator.length());
    }

    QString extractBaseName(const EmotePtr &emote)
    {
        int index = emote->tooltip.string.indexOf(beforeBaseName);
        if (index >= 0)
        {
            // the emote was aliased before
            int startPos = index + beforeBaseName.length();
            int brIndex = emote->tooltip.string.indexOf(breakTag, startPos);
            return emote->tooltip.string.mid(startPos, brIndex - startPos);
        }
        return emote->name.string;
    }

    Tooltip updateTooltip(const EmotePtr &oldEmote, const EmotePtr &cached,
                          const EventApiEmoteUpdateDispatch &dispatch)
    {
        if (cached)
        {
            return createAliasedTooltip(dispatch.emoteName, cached->name.string,
                                        extractCreator(cached->tooltip), false);
        }
        return createAliasedTooltip(dispatch.emoteName,
                                    extractBaseName(oldEmote),
                                    extractCreator(oldEmote->tooltip), false);
    }

    EmotePtr createUpdatedEmote(const EmotePtr &oldEmote,
                                const EventApiEmoteUpdateDispatch &dispatch)
    {
        auto cached = v3Cache.getEmote({dispatch.emoteId});
        if (cached && dispatch.emoteName == cached->name.string)
        {
            // Emote name changed to the original name.
            return cached;
        }
        auto emote = std::make_shared<const Emote>(
            Emote{{dispatch.emoteName},
                  oldEmote->images,
                  updateTooltip(oldEmote, cached, dispatch),
                  oldEmote->homePage,
                  oldEmote->zeroWidth});
        return emote;
    }

}  // namespace

SeventvEmotes::SeventvEmotes()
    : global_(std::make_shared<EmoteMap>())
{
}

std::shared_ptr<const EmoteMap> SeventvEmotes::emotes() const
{
    return this->global_.get();
}

boost::optional<EmotePtr> SeventvEmotes::emote(const EmoteName &name) const
{
    auto emotes = this->global_.get();
    auto it = emotes->find(name);

    if (it == emotes->end())
    {
        return boost::none;
    }
    return it->second;
}

void SeventvEmotes::loadEmotes()
{
    if (!Settings::instance().enableSevenTVGlobalEmotes)
    {
        this->global_.set(EMPTY_EMOTE_MAP);
        return;
    }

    qCDebug(chatterinoSeventv) << "Loading 7TV Emotes";

    QJsonObject payload, variables;

    QString query = R"(
        query loadGlobalEmotes($query: String!, $globalState: String, $page: Int, $limit: Int, $pageSize: Int) {
        search_emotes(query: $query, globalState: $globalState, page: $page, limit: $limit, pageSize: $pageSize) {
            id
            name
            provider
            provider_id
            visibility
            mime
            height
            owner {
                id
                display_name
                login
                twitch_id
            }
        }
    })";

    variables.insert("query", QString());
    variables.insert("globalState", "only");
    variables.insert("page", 1);  // TODO(zneix): Add support for pagination
    variables.insert("limit", maxPageSize);
    variables.insert("pageSize", maxPageSize);

    payload.insert("query", query.replace(whitespaceRegex, " "));
    payload.insert("variables", variables);

    NetworkRequest(apiUrlGQL, NetworkRequestType::Post)
        .timeout(30000)
        .header("Content-Type", "application/json")
        .payload(QJsonDocument(payload).toJson(QJsonDocument::Compact))
        .onSuccess([this](NetworkResult result) -> Outcome {
            QJsonArray parsedEmotes = result.parseJson()
                                          .value("data")
                                          .toObject()
                                          .value("search_emotes")
                                          .toArray();
            qCDebug(chatterinoSeventv)
                << "7TV Global Emotes" << parsedEmotes.size();

            auto pair = parseGlobalEmotes(parsedEmotes, *this->global_.get());
            if (pair.first)
                this->global_.set(
                    std::make_shared<EmoteMap>(std::move(pair.second)));
            return pair.first;
        })
        .execute();
}

boost::optional<EmotePtr> SeventvEmotes::addEmote(
    Atomic<std::shared_ptr<const EmoteMap>> &map,
    const EventApiEmoteAddDispatch &dispatch)
{
    // Check for visibility first, so we don't copy the map.
    auto emoteData = dispatch.emoteJson["data"].toObject();
    if (!checkEmoteVisibilityV3(emoteData))
    {
        return boost::none;
    }

    EmoteMap updatedMap = *map.get();
    std::lock_guard<std::mutex> guard(v3CacheMutex);
    auto emote = createEmoteV3(dispatch.emoteJson, emoteData, false);
    updatedMap[emote->name] = emote;
    updateEmoteMapPtr(map, std::move(updatedMap));

    return emote;
}

boost::optional<EmotePtr> SeventvEmotes::updateEmote(
    Atomic<std::shared_ptr<const EmoteMap>> &map,
    const EventApiEmoteUpdateDispatch &dispatch)
{
    auto oldMap = map.get();
    auto oldEmote = oldMap->find({dispatch.oldEmoteName});
    if (oldEmote == oldMap->end())
    {
        return boost::none;
    }
    EmoteMap updatedMap = *map.get();
    updatedMap.erase(oldEmote->second->name);

    std::lock_guard<std::mutex> guard(v3CacheMutex);
    auto emote = createUpdatedEmote(oldEmote->second, dispatch);
    updatedMap[emote->name] = emote;
    updateEmoteMapPtr(map, std::move(updatedMap));

    return emote;
}

bool SeventvEmotes::removeEmote(Atomic<std::shared_ptr<const EmoteMap>> &map,
                                const EventApiEmoteRemoveDispatch &dispatch)
{
    EmoteMap updatedMap = *map.get();
    // TODO: this is wrong for aliased emotes.
    auto it = updatedMap.find(EmoteName{dispatch.emoteName});
    if (it == updatedMap.end())
    {
        // We already copied the map at this point and are now discarding the copy.
        // This is fine, because this case should be really rare.
        return false;
    }
    updatedMap.erase(it);
    updateEmoteMapPtr(map, std::move(updatedMap));
    return true;
}

void SeventvEmotes::loadChannel(
    std::weak_ptr<Channel> channel, const QString &channelId,
    std::function<void(EmoteMap &&, SeventvEmotes::ChannelInfo)> callback,
    bool manualRefresh)
{
    qCDebug(chatterinoSeventv)
        << "Reloading 7TV Channel Emotes" << channelId << manualRefresh;

    NetworkRequest(apiUrlChannelEmotes.arg(channelId), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess([callback = std::move(callback), channel, channelId,
                    manualRefresh](NetworkResult result) -> Outcome {
            auto json = result.parseJson();
            auto emoteSet = json["emote_set"].toObject();
            auto parsedEmotes = emoteSet["emotes"].toArray();

            auto emoteMap = parseChannelEmotes(parsedEmotes);
            bool hasEmotes = !emoteMap.empty();

            qCDebug(chatterinoSeventv)
                << "Loaded 7TV Channel Emotes" << channelId << emoteMap.size()
                << manualRefresh;

            if (hasEmotes)
            {
                callback(std::move(emoteMap),
                         {json["user"].toObject()["id"].toString(),
                          emoteSet["id"].toString()});
            }
            if (auto shared = channel.lock(); manualRefresh)
            {
                if (hasEmotes)
                {
                    shared->addMessage(
                        makeSystemMessage("7TV channel emotes reloaded."));
                }
                else
                {
                    shared->addMessage(
                        makeSystemMessage(CHANNEL_HAS_NO_EMOTES));
                }
            }
            return Success;
        })
        .onError([channelId, channel, manualRefresh](NetworkResult result) {
            auto shared = channel.lock();
            if (!shared)
                return;
            if (result.status() == 400)
            {
                qCWarning(chatterinoSeventv)
                    << "Error occured fetching 7TV emotes: "
                    << result.parseJson();
                if (manualRefresh)
                    shared->addMessage(
                        makeSystemMessage(CHANNEL_HAS_NO_EMOTES));
            }
            else if (result.status() == NetworkResult::timedoutStatus)
            {
                // TODO: Auto retry in case of a timeout, with a delay
                qCWarning(chatterinoSeventv())
                    << "Fetching 7TV emotes for channel" << channelId
                    << "failed due to timeout";
                shared->addMessage(makeSystemMessage(
                    "Failed to fetch 7TV channel emotes. (timed out)"));
            }
            else
            {
                qCWarning(chatterinoSeventv)
                    << "Error fetching 7TV emotes for channel" << channelId
                    << ", error" << result.status();
                shared->addMessage(
                    makeSystemMessage("Failed to fetch 7TV channel "
                                      "emotes. (unknown error)"));
            }
        })
        .execute();
}

}  // namespace chatterino
