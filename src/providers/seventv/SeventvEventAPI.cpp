#include "providers/seventv/SeventvEventAPI.hpp"

#include "Application.hpp"
#include "providers/seventv/eventapi/SeventvEventAPIClient.hpp"
#include "providers/seventv/eventapi/SeventvEventAPIDispatch.hpp"
#include "providers/seventv/eventapi/SeventvEventAPIMessage.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "util/PostToThread.hpp"

#include <QJsonArray>

#include <utility>

namespace chatterino {

SeventvEventAPI::SeventvEventAPI(
    QString host, std::chrono::milliseconds defaultHeartbeatInterval)
    : BasicPubSubManager(std::move(host))
    , heartbeatInterval_(defaultHeartbeatInterval)
{
}

void SeventvEventAPI::subscribeUser(const QString &userID,
                                    const QString &emoteSetID)
{
    if (!userID.isEmpty() && this->subscribedUsers_.insert(userID).second)
    {
        this->subscribe({SeventvEventAPIObjectIDCondition{userID},
                         SeventvEventAPISubscriptionType::UpdateUser});
    }
    if (!emoteSetID.isEmpty() &&
        this->subscribedEmoteSets_.insert(emoteSetID).second)
    {
        this->subscribe({SeventvEventAPIObjectIDCondition{userID},
                         SeventvEventAPISubscriptionType::UpdateEmoteSet});
    }
}

void SeventvEventAPI::subscribeTwitchChannel(const QString &id)
{
    if (this->subscribedTwitchChannels_.insert(id).second)
    {
        this->subscribe({SeventvEventAPIChannelCondition{id},
                         SeventvEventAPISubscriptionType::AnyCosmetic});
        this->subscribe({SeventvEventAPIChannelCondition{id},
                         SeventvEventAPISubscriptionType::AnyEntitlement});
    }
}

void SeventvEventAPI::unsubscribeEmoteSet(const QString &id)
{
    if (this->subscribedEmoteSets_.erase(id) > 0)
    {
        this->unsubscribe({SeventvEventAPIObjectIDCondition{id},
                           SeventvEventAPISubscriptionType::UpdateEmoteSet});
    }
}

void SeventvEventAPI::unsubscribeUser(const QString &id)
{
    if (this->subscribedUsers_.erase(id) > 0)
    {
        this->unsubscribe({SeventvEventAPIObjectIDCondition{id},
                           SeventvEventAPISubscriptionType::UpdateUser});
    }
}

void SeventvEventAPI::unsubscribeTwitchChannel(const QString &id)
{
    if (this->subscribedTwitchChannels_.erase(id) > 0)
    {
        this->unsubscribe({SeventvEventAPIChannelCondition{id},
                           SeventvEventAPISubscriptionType::AnyCosmetic});
        this->unsubscribe({SeventvEventAPIChannelCondition{id},
                           SeventvEventAPISubscriptionType::AnyEntitlement});
    }
}

std::shared_ptr<BasicPubSubClient<SeventvEventAPISubscription>>
    SeventvEventAPI::createClient(liveupdates::WebsocketClient &client,
                                  websocketpp::connection_hdl hdl)
{
    auto shared = std::make_shared<SeventvEventAPIClient>(
        client, hdl, this->heartbeatInterval_);
    return std::static_pointer_cast<
        BasicPubSubClient<SeventvEventAPISubscription>>(std::move(shared));
}

void SeventvEventAPI::onMessage(
    websocketpp::connection_hdl hdl,
    BasicPubSubManager<SeventvEventAPISubscription>::WebsocketMessagePtr msg)
{
    const auto &payload = QString::fromStdString(msg->get_payload());

    auto pMessage = parseSeventvEventAPIBaseMessage(payload);

    if (!pMessage)
    {
        qCDebug(chatterinoSeventvEventAPI)
            << "Unable to parse incoming event-api message: " << payload;
        return;
    }
    auto message = *pMessage;
    switch (message.op)
    {
        case SeventvEventAPIOpcode::Hello: {
            if (auto client = this->findClient(hdl))
            {
                if (auto *stvClient =
                        dynamic_cast<SeventvEventAPIClient *>(client.get()))
                {
                    stvClient->setHeartbeatInterval(
                        message.data["heartbeat_interval"].toInt());
                }
            }
        }
        break;
        case SeventvEventAPIOpcode::Heartbeat: {
            if (auto client = this->findClient(hdl))
            {
                if (auto *stvClient =
                        dynamic_cast<SeventvEventAPIClient *>(client.get()))
                {
                    stvClient->handleHeartbeat();
                }
            }
        }
        break;
        case SeventvEventAPIOpcode::Dispatch: {
            auto dispatch = message.toInner<SeventvEventAPIDispatch>();
            if (!dispatch)
            {
                qCDebug(chatterinoSeventvEventAPI)
                    << "Malformed dispatch" << payload;
                return;
            }
            this->handleDispatch(*dispatch);
        }
        break;
        case SeventvEventAPIOpcode::Reconnect: {
            if (auto client = this->findClient(hdl))
            {
                if (auto *stvClient =
                        dynamic_cast<SeventvEventAPIClient *>(client.get()))
                {
                    stvClient->close("Reconnecting");
                }
            }
        }
        break;
        case SeventvEventAPIOpcode::Ack: {
            // unhandled
        }
        break;
        default: {
            qCDebug(chatterinoSeventvEventAPI) << "Unhandled op: " << payload;
        }
        break;
    }
}

void SeventvEventAPI::handleDispatch(const SeventvEventAPIDispatch &dispatch)
{
    switch (dispatch.type)
    {
        case SeventvEventAPISubscriptionType::UpdateEmoteSet: {
            // dispatchBody: {
            //   pushed:  Array<{ key, value            }>,
            //   pulled:  Array<{ key,        old_value }>,
            //   updated: Array<{ key, value, old_value }>,
            // }
            for (const auto pushedRef : dispatch.body["pushed"].toArray())
            {
                auto pushed = pushedRef.toObject();
                if (pushed["key"].toString() != "emotes")
                {
                    continue;
                }

                const SeventvEventAPIEmoteAddDispatch added(
                    dispatch, pushed["value"].toObject());

                if (added.validate())
                {
                    this->signals_.emoteAdded.invoke(added);
                }
                else
                {
                    qCDebug(chatterinoSeventvEventAPI)
                        << "Invalid dispatch" << dispatch.body;
                }
            }
            for (const auto updatedRef : dispatch.body["updated"].toArray())
            {
                auto updated = updatedRef.toObject();
                if (updated["key"].toString() != "emotes")
                {
                    continue;
                }

                const SeventvEventAPIEmoteUpdateDispatch update(
                    dispatch, updated["old_value"].toObject(),
                    updated["value"].toObject());

                if (update.validate())
                {
                    this->signals_.emoteUpdated.invoke(update);
                }
                else
                {
                    qCDebug(chatterinoSeventvEventAPI)
                        << "Invalid dispatch" << dispatch.body;
                }
            }
            for (const auto pulledRef : dispatch.body["pulled"].toArray())
            {
                auto pulled = pulledRef.toObject();
                if (pulled["key"].toString() != "emotes")
                {
                    continue;
                }

                const SeventvEventAPIEmoteRemoveDispatch removed(
                    dispatch, pulled["old_value"].toObject());

                if (removed.validate())
                {
                    this->signals_.emoteRemoved.invoke(removed);
                }
                else
                {
                    qCDebug(chatterinoSeventvEventAPI)
                        << "Invalid dispatch" << dispatch.body;
                }
            }
        }
        break;
        case SeventvEventAPISubscriptionType::UpdateUser: {
            // dispatchBody: {
            //   updated: Array<{ key, value: Array<{key, value}> }>
            // }
            for (const auto updatedRef : dispatch.body["updated"].toArray())
            {
                auto updated = updatedRef.toObject();
                if (updated["key"].toString() != "connections")
                {
                    continue;
                }
                for (const auto valueRef : updated["value"].toArray())
                {
                    auto value = valueRef.toObject();
                    if (value["key"].toString() != "emote_set")
                    {
                        continue;
                    }

                    const SeventvEventAPIUserConnectionUpdateDispatch update(
                        dispatch, value, (size_t)updated["index"].toInt());

                    if (update.validate())
                    {
                        this->signals_.userUpdated.invoke(update);
                    }
                    else
                    {
                        qCDebug(chatterinoSeventvEventAPI)
                            << "Invalid dispatch" << dispatch.body;
                    }
                }
            }
        }
        break;
        case SeventvEventAPISubscriptionType::CreateCosmetic: {
            const SeventvEventAPICosmeticCreateDispatch cosmetic(dispatch);
            if (cosmetic.validate())
            {
                postToThread([cosmetic]() {
                    switch (cosmetic.kind)
                    {
                        case SeventvCosmeticKind::Badge: {
                            getApp()->seventvBadges->addBadge(cosmetic.data);
                        }
                        break;
                        case SeventvCosmeticKind::Paint: {
                            getApp()->seventvPaints->addPaint(cosmetic.data);
                        }
                        break;
                        default:
                            break;
                    }
                });
            }
            else
            {
                qCDebug(chatterinoSeventvEventAPI)
                    << "Invalid cosmetic dispatch" << dispatch.body;
            }
        }
        break;
        case SeventvEventAPISubscriptionType::CreateEntitlement: {
            const SeventvEventAPIEntitlementCreateDispatch entitlement(
                dispatch);
            if (entitlement.validate())
            {
                postToThread([entitlement]() {
                    switch (entitlement.kind)
                    {
                        case SeventvCosmeticKind::Badge: {
                            getApp()->seventvBadges->assignBadgeToUser(
                                entitlement.refID, UserId{entitlement.userID});
                        }
                        break;
                        case SeventvCosmeticKind::Paint: {
                            getApp()->seventvPaints->assignPaintToUser(
                                entitlement.refID,
                                UserName{entitlement.userName});
                        }
                        break;
                        default:
                            break;
                    }
                });
            }
            else
            {
                qCDebug(chatterinoSeventvEventAPI)
                    << "Invalid entitlement dispatch" << dispatch.body;
            }
        }
        break;
        default: {
            qCDebug(chatterinoSeventvEventAPI)
                << "Unknown subscription type:"
                << magic_enum::enum_name(dispatch.type).data()
                << "body:" << dispatch.body;
        }
        break;
    }
}

}  // namespace chatterino
