#include "util/BoostJsonWrap.hpp"

#include <boost/json/string.hpp>
#include <QString>

namespace chatterino {

BoostJsonObject BoostJsonValue::toObject() const
{
    if (!this->v)
    {
        return {};
    }
    return this->v->if_object();
}

BoostJsonArray BoostJsonValue::toArray() const
{
    if (!this->v)
    {
        return {};
    }
    return this->v->if_array();
}

QString BoostJsonValue::toQString(const QString &defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *s = this->v->if_string();
    if (!s)
    {
        return defaultValue;
    }
    return QString::fromUtf8(s->data(), static_cast<qsizetype>(s->size()));
}

std::string BoostJsonValue::toStdString(const std::string &defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *s = this->v->if_string();
    if (!s)
    {
        return defaultValue;
    }
    return {s->data(), s->size()};
}

std::string_view BoostJsonValue::toStringView(
    std::string_view defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *s = this->v->if_string();
    if (!s)
    {
        return defaultValue;
    }
    return {s->data(), s->size()};
}

int64_t BoostJsonValue::toInt64(int64_t defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *i = this->v->if_int64();
    if (!i)
    {
        return defaultValue;
    }
    return *i;
}

uint64_t BoostJsonValue::toUint64(uint64_t defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *i = this->v->if_int64();
    if (!i || *i < 0)
    {
        return defaultValue;
    }
    return static_cast<uint64_t>(*i);
}

bool BoostJsonValue::toBool(bool defaultValue) const
{
    if (!this->v)
    {
        return defaultValue;
    }
    const auto *i = this->v->if_bool();
    if (!i)
    {
        return defaultValue;
    }
    return *i;
}

BoostJsonValue BoostJsonValue::operator[](size_t i) const
{
    if (!this->v)
    {
        return {};
    }
    const auto *a = this->v->if_array();
    if (!a || i >= a->size())
    {
        return {};
    }
    return {a[i]};
}

BoostJsonValue BoostJsonValue::operator[](std::string_view key) const
{
    if (!this->v)
    {
        return {};
    }
    const auto *o = this->v->if_object();
    if (!o)
    {
        return {};
    }
    const auto *it = o->find(key);
    if (it == o->end())
    {
        return {};
    }
    return {it->value()};
}

bool BoostJsonObject::contains(std::string_view key) const
{
    if (!this->o)
    {
        return false;
    }
    return this->o->contains(key);
}

BoostJsonValue BoostJsonObject::value(std::string_view key) const
{
    if (!this->o)
    {
        return {};
    }
    const auto *it = this->o->find(key);
    if (it == this->o->end())
    {
        return {};
    }
    return {it->value()};
}

BoostJsonValue BoostJsonArray::at(size_t i) const
{
    if (!this->a || i >= this->a->size())
    {
        return {};
    }
    return {this->a[i]};
}

}  // namespace chatterino
