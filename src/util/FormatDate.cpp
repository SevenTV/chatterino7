#include "FormatDate.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

    QString formatDate(const QDateTime &input)
    {
        switch (getSettings()->dateFormat)
        {
            case DateFormat::YearMonthDayHyphen:
                return input.toString("yyyy-MM-dd");
            case DateFormat::DayMonthYearDot:
                return input.toString("dd.MM.yyyy");
            case DateFormat::DayMonthYearSlash:
                return input.toString("dd/MM/yyyy");
            default:
                return input.toString("yyyy-MM-dd");
        }
    }

    QString formatDate(const QDate &input)
    {
        const QDateTime dt(input, QTime(0,0));
        return formatDate(dt);
    }

}