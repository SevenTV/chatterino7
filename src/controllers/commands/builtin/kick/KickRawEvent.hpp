#pragma once

class QString;

namespace chatterino {

struct CommandContext;

}  // namespace chatterino

namespace chatterino::commands {

/// /debug-kick-raw-event
QString debugKickRawEvent(const CommandContext &ctx);

}  // namespace chatterino::commands
