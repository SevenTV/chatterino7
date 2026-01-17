#include "controllers/commands/builtin/kick/KickRawEvent.hpp"

#include "Application.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "util/BoostJsonWrap.hpp"

#include <boost/json/parse.hpp>

namespace chatterino::commands {

using namespace Qt::Literals;

QString debugKickRawEvent(const CommandContext &ctx)
{
    if (!ctx.kickChannel)
    {
        return u"Not a Kick channel."_s;
    }
    if (ctx.words.length() <= 2)
    {
        return u"Usage: /debug-kick-raw-event <event-name> <event-data>"_s;
    }

    auto eventName = ctx.words.at(1).toStdString();
    auto jsonText = ctx.words.mid(2).join(' ').toStdString();
    boost::system::error_code ec;
    auto jv = boost::json::parse(jsonText, ec);
    if (ec)
    {
        return u"Failed to parse JSON: "_s + QString::fromStdString(ec.what());
    }
    bool handled = getApp()->getKickChatServer()->onAppEvent(
        ctx.kickChannel->roomID(), 0, eventName, BoostJsonValue(jv).toObject());
    if (handled)
    {
        return {};
    }
    return u"Unhandled event."_s;
}

}  // namespace chatterino::commands
