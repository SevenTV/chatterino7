#pragma once

#include "common/Singleton.hpp"
#include "providers/seventv/paints/Paint.hpp"
#include "providers/seventv/paints/PaintDropShadow.hpp"

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
    QGradientStops parsePaintStops(QJsonArray stops);
    std::vector<PaintDropShadow> parseDropShadows(QJsonArray dropShadows);

    QColor decimalColorToQColor(const uint32_t color);

    std::map<QString, Paint *> paints;
};

}  // namespace chatterino
