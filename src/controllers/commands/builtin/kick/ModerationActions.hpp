#pragma once

class QString;

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// /ban (Kick)
QString doKickBan(const CommandContext &ctx);

/// /timeout (Kick)
QString doKickTimeout(const CommandContext &ctx);

/// /unban (Kick)
QString doKickUnban(const CommandContext &ctx);

/// /delete (Kick)
QString doKickDelete(const CommandContext &ctx);

}  // namespace chatterino::commands
