#pragma once

#include <QDateTime>
#include <QString>

namespace chatterino {
    QString formatDate(const QDate &input);
    QString formatDate(const QDateTime &input);
}
