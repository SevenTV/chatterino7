#pragma once

#include "common/Aliases.hpp"
#include "providers/seventv/paints/Paint.hpp"

#include <QJsonArray>
#include <QString>

#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace chatterino {

class SeventvPaints
{
public:
    SeventvPaints();

    void addPaint(const QJsonObject &paintJson);
    void assignPaintToUser(const QString &paintID,
                           const UserName &twitchUserName,
                           const UserName &kickUserName);
    void clearPaintFromUser(const QString &paintID,
                            const UserName &twitchUserName,
                            const UserName &kickUserName);

    std::shared_ptr<Paint> getPaint(const QString &userName, bool kick) const;

private:
    // Mutex for both `paintMap_` and `knownPaints_`
    mutable std::shared_mutex mutex_;

    // user-name => paint
    std::unordered_map<QString, std::shared_ptr<Paint>> kickPaintMap_;
    // user-name => paint
    std::unordered_map<QString, std::shared_ptr<Paint>> twitchPaintMap_;
    // paint-id => paint
    std::unordered_map<QString, std::shared_ptr<Paint>> knownPaints_;
};

}  // namespace chatterino
