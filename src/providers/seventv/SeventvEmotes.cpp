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

    ImageSet makeImageSet(const QJsonObject &jsonEmote)
    {
        auto host = jsonEmote["host"].toObject();
        auto baseUrl = host["url"].toString();
        ImageSet set;
        int nextSize = 1;
        double baseWidth = -1;
        // assume the files array is sorted
        for (auto fileItem : host["files"].toArray())
        {
            auto file = fileItem.toObject();
            if (file["format"].toString() != "WEBP")
            {
                continue;
            }
            double width = file["width"].toDouble();
            double scale = 1.0;
            // this expects the array to be sorted
            if (baseWidth < 0.0)
            {
                baseWidth = width;
            }
            else
            {
                scale = baseWidth / width;
            }
            auto image = Image::fromUrl(
                {QString("https:%1/%2").arg(baseUrl, file["name"].toString())},
                scale);
            switch (nextSize)
            {
                case 1: {
                    set.setImage1(image);
                }
                break;
                case 2: {
                    set.setImage2(image);
                }
                break;
                case 3: {
                    set.setImage3(image);
                }
                break;
                case 4: {
                    set.setImage4(image);
                    break;
                }
            }
            nextSize++;
        }

        return set;
    }

    Tooltip createTooltip(const QString &name, const QString &author,
                          bool isGlobal)
    {
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
     * Creates a "regular" (i.e. not aliased or global) emote
     * that will be added to the cache.
     */
    Emote createV3Emote(const EmoteId &id, const QJsonObject &emoteData,
                        ImageSet *imageSet)
    {
        auto name = EmoteName{emoteData.value("name").toString()};
        auto author = EmoteAuthor{emoteData.value("owner")
                                      .toObject()
                                      .value("display_name")
                                      .toString()};
        auto flags = SeventvEmoteFlags(
            SeventvEmoteFlag(emoteData.value("flags").toInt()));
        bool zeroWidth = flags.has(SeventvEmoteFlag::ZeroWidth);

        auto emote = Emote(
            {name, imageSet != nullptr ? *imageSet : makeImageSet(emoteData),
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
                                       ImageSet *imageSet)
    {
        auto name = EmoteName{addedEmote.value("name").toString()};
        auto author = EmoteAuthor{emoteData.value("owner")
                                      .toObject()
                                      .value("display_name")
                                      .toString()};
        auto flags = SeventvActiveEmoteFlags(
            SeventvActiveEmoteFlag(addedEmote.value("flags").toInt()));
        bool zeroWidth = flags.has(SeventvActiveEmoteFlag::ZeroWidth);
        bool isAliased =
            addedEmote["name"].toString() != emoteData["name"].toString();
        auto tooltip = isAliased
                           ? createAliasedTooltip(name.string,
                                                  emoteData["name"].toString(),
                                                  author.string, false)
                           : createTooltip(name.string, author.string, false);

        auto emote = Emote(
            {name, imageSet != nullptr ? *imageSet : makeImageSet(emoteData),
             tooltip, Url{emoteLinkFormat.arg(id.string)}, zeroWidth});

        return emote;
    }

    /**
     * Creates an aliased or global emote where
     * the base emote is cached.
     */
    Emote createV3ForkedEmote(const QJsonObject &addedEmote,
                              const EmotePtr &baseEmote, const QString &author)
    {
        auto name = EmoteName{addedEmote.value("name").toString()};
        bool isAliased =
            addedEmote["name"].toString() != baseEmote->name.string;
        auto tooltip =
            isAliased ? createAliasedTooltip(
                            name.string, baseEmote->name.string, author, false)
                      : createTooltip(name.string, author, false);
        auto flags = SeventvActiveEmoteFlags(
            SeventvActiveEmoteFlag(addedEmote.value("flags").toInt()));
        bool zeroWidth = flags.has(SeventvActiveEmoteFlag::ZeroWidth);

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

    bool checkEmoteVisibility(const QJsonObject &emoteJson)
    {
        int64_t visibility = emoteJson.value("visibility").toInt();
        auto visibilityFlags =
            SeventvEmoteVisibilityFlags(SeventvEmoteVisibilityFlag(visibility));
        return !visibilityFlags.has(SeventvEmoteVisibilityFlag::Unlisted) ||
               getSettings()->showUnlistedEmotes;
    }

    bool checkEmoteVisibilityV3(const QJsonObject &emoteData)
    {
        return emoteData.value("listed").toBool() ||
               getSettings()->showUnlistedEmotes;
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
            auto emoteId = EmoteId{addedEmote["id"].toString()};
            bool isAliased =
                addedEmote["name"].toString() != emoteData["name"].toString();
            auto cached = v3Cache.getEmote(emoteId);
            if (cached)
            {
                if (isAliased)
                {
                    // We don't cache aliased emotes.
                    auto emote = std::make_shared<Emote>(
                        createV3ForkedEmote(addedEmote, cached,
                                            emoteData["owner"]
                                                .toObject()["display_name"]
                                                .toString()));
                    emotes[cached->name] = emote;
                }
                else
                {
                    emotes[cached->name] = cached;
                }
            }
            else
            {
                auto *cachedImages = v3Cache.getImageSet(emoteId);
                if (isAliased)
                {
                    // We don't cache aliased emotes...
                    auto emote = std::make_shared<Emote>(
                        createV3AliasedOrGlobalEmote(emoteId, false, addedEmote,
                                                     emoteData, cachedImages));
                    if (cachedImages == nullptr)
                    {
                        // ...however, we do cache the images.
                        v3Cache.putImageSet(emoteId, emote->images);
                    }
                    emotes[emote->name] = emote;
                }
                else
                {
                    auto emote = v3Cache.putEmote(
                        emoteId,
                        createV3Emote(emoteId, emoteData, cachedImages));
                    emotes[emote->name] = emote;
                }
            }
        }

        return emotes;
    }

    void updateEmoteMapPtr(Atomic<std::shared_ptr<const EmoteMap>> &map,
                           EmoteMap &&updatedMap)
    {
        map.set(std::make_shared<EmoteMap>(std::move(updatedMap)));
    }

    boost::optional<EmotePtr> findEmote(
        const std::shared_ptr<const EmoteMap> &map,
        const QString &emoteBaseName, const QJsonObject &emoteJson)
    {
        auto id = emoteJson["id"].toString();

        // Step 1: check if the emote is added with the base name
        auto mapIt = map->find(EmoteName{emoteBaseName});
        // We still need to check for the id!
        if (mapIt != map->end() && mapIt->second->homePage.string.endsWith(id))
        {
            return mapIt->second;
        }

        std::lock_guard<std::mutex> guard(emoteCacheMutex);

        // Step 2: check the cache for the emote
        auto emote = emoteCache[EmoteId{id}].lock();
        if (emote)
        {
            auto cacheIt = map->find(emote->name);
            // Same as above, make sure it's actually the correct emote
            if (cacheIt != map->end() &&
                cacheIt->second->homePage.string.endsWith(id))
            {
                return cacheIt->second;
            }
        }
        // Step 3: Since the emote is not added, and it's not even in the cache,
        //         we need to check if the emote is added.
        //         This is expensive but the cache entry may be corrupted
        //         when an emote was added with a different alias in some other
        //         channel.
        for (const auto &[_, value] : *map)
        {
            // since the url ends with the emote id we can check this
            if (value->homePage.string.endsWith(id))
            {
                return value;
            }
        }
        return boost::none;
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
        return boost::none;
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
    Atomic<std::shared_ptr<const EmoteMap>> &map, const QJsonValue &emoteJson)
{
    // Check for visibility first, so we don't copy the map.
    if (!checkEmoteVisibility(emoteJson.toObject()))
    {
        return boost::none;
    }

    EmoteMap updatedMap = *map.get();
    auto emote = createEmote(emoteJson, false);
    auto emotePtr = cachedOrMake(std::move(emote.emote), emote.id);
    updatedMap[emote.name] = emotePtr;
    updateEmoteMapPtr(map, std::move(updatedMap));

    return emotePtr;
}

boost::optional<EmotePtr> SeventvEmotes::updateEmote(
    Atomic<std::shared_ptr<const EmoteMap>> &map, QString *emoteBaseName,
    const QJsonValue &emoteJson)
{
    auto oldMap = map.get();
    auto foundEmote = findEmote(oldMap, *emoteBaseName, emoteJson.toObject());
    if (!foundEmote)
    {
        return boost::none;
    }

    *emoteBaseName = foundEmote->get()->getCopyString();

    EmoteMap updatedMap = *map.get();
    updatedMap.erase(foundEmote.value()->name);

    auto emote = createEmote(emoteJson, false);
    auto emotePtr = cachedOrMake(std::move(emote.emote), emote.id);
    updatedMap[emote.name] = emotePtr;
    updateEmoteMapPtr(map, std::move(updatedMap));

    return emotePtr;
}

bool SeventvEmotes::removeEmote(Atomic<std::shared_ptr<const EmoteMap>> &map,
                                const QString &emoteName)
{
    EmoteMap updatedMap = *map.get();
    auto it = updatedMap.find(EmoteName{emoteName});
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

void SeventvEmotes::loadChannel(std::weak_ptr<Channel> channel,
                                const QString &channelId,
                                std::function<void(EmoteMap &&)> callback,
                                bool manualRefresh)
{
    qCDebug(chatterinoSeventv)
        << "Reloading 7TV Channel Emotes" << channelId << manualRefresh;

    NetworkRequest(apiUrlChannelEmotes.arg(channelId), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess([callback = std::move(callback), channel, channelId,
                    manualRefresh](NetworkResult result) -> Outcome {
            auto parsedEmotes = result.parseJson()
                                    .value("emote_set")
                                    .toObject()
                                    .value("emotes")
                                    .toArray();

            auto emoteMap = parseChannelEmotes(parsedEmotes);
            bool hasEmotes = !emoteMap.empty();

            qCDebug(chatterinoSeventv)
                << "Loaded 7TV Channel Emotes" << channelId << emoteMap.size()
                << manualRefresh;

            if (hasEmotes)
            {
                callback(std::move(emoteMap));
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
