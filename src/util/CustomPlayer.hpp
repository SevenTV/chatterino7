// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringView>

namespace chatterino {

void openInCustomPlayer(QStringView channel,
                        QStringView prefixURL = u"https://www.twitch.tv/");

}  // namespace chatterino
