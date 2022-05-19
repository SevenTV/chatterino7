#pragma once

#include "common/Singleton.hpp"
#include "providers/seventv/paints/Paint.hpp"

#include <optional>

#include <QJsonArray>
#include <QString>

namespace chatterino {

class SeventvPaints : public Singleton
{
public:
    virtual void initialize(Settings &settings, Paths &paths) override;

    std::optional<Paint *> getPaint(const QString &userName);

private:
    void loadSeventvCosmetics();
    void loadSeventvPaints(QJsonArray paints);

    QStringList parsePaintUsers(QJsonArray users);
    std::optional<QColor> parsePaintColor(QJsonValue color);
    std::vector<std::pair<float, QColor>> parsePaintStops(QJsonArray stops);

    QColor decimalColorToQColor(const uint32_t color);

    std::map<QString, Paint *> paints;
};

}  // namespace chatterino
