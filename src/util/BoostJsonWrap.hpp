#pragma once

#include <boost/json/value.hpp>
#include <QString>

#if __has_cpp_attribute(gsl::Pointer)
#    define CHATTERINO_GSL_POINTER [[gsl::Pointer]]
#else
#    define CHATTERINO_GSL_POINTER
#endif

namespace chatterino {

class BoostJsonObject;
class BoostJsonArray;

/// This is a small wrapper around `const boost::json::value*` which handles
/// missing or unexpected values gracefully.
///
/// It's similar to `QJsonValue` with the big difference that this class is a
/// reference.
class CHATTERINO_GSL_POINTER BoostJsonValue
{
public:
    BoostJsonValue(const boost::json::value &v)
        : v(std::addressof(v))
    {
    }
    constexpr BoostJsonValue() = default;

    constexpr bool isUndefined() const
    {
        return this->v == nullptr;
    }

    constexpr bool isObject() const
    {
        return this->v != nullptr && this->v->is_object();
    }

    constexpr bool isArray() const
    {
        return this->v != nullptr && this->v->is_array();
    }

    constexpr bool isInt64() const
    {
        return this->v != nullptr && this->v->is_int64();
    }

    constexpr bool isString() const
    {
        return this->v != nullptr && this->v->is_string();
    }

    BoostJsonObject toObject() const;
    BoostJsonArray toArray() const;
    QString toQString(const QString &defaultValue = {}) const;
    std::string toStdString(const std::string &defaultValue = {}) const;
    std::string_view toStringView(std::string_view defaultValue = {}) const;
    int64_t toInt64(int64_t defaultValue = 0) const;
    uint64_t toUint64(uint64_t defaultValue = 0) const;
    bool toBool(bool defaultValue = false) const;

    BoostJsonValue operator[](size_t i) const;
    BoostJsonValue operator[](std::string_view key) const;

private:
    constexpr BoostJsonValue(const boost::json::value *v)
        : v(v)
    {
    }

    const boost::json::value *v = nullptr;

    friend BoostJsonObject;
    friend BoostJsonArray;
};

/// This is a small wrapper around `const boost::json::object*` which handles
/// missing or unexpected values gracefully.
///
/// It's similar to `QJsonObject` with the big difference that this class is a
/// reference.
class CHATTERINO_GSL_POINTER BoostJsonObject
{
public:
    BoostJsonObject(const boost::json::object &o)
        : o(std::addressof(o))
    {
    }
    constexpr BoostJsonObject() = default;

    // FIXME: add iterators

    bool empty() const
    {
        return this->o == nullptr || this->o->empty();
    }
    size_t size() const
    {
        if (!this->o)
        {
            return 0;
        }
        return this->o->size();
    }

    bool contains(std::string_view key) const;

    BoostJsonValue value(std::string_view key) const;
    BoostJsonValue operator[](std::string_view key) const
    {
        return this->value(key);
    }

private:
    constexpr BoostJsonObject(const boost::json::object *o)
        : o(o)
    {
    }

    const boost::json::object *o = nullptr;

    friend BoostJsonValue;
};

/// This is a small wrapper around `const boost::json::array*` which handles
/// missing or unexpected values gracefully.
///
/// It's similar to `QJsonArray` with the big difference that this class is a
/// reference.
class CHATTERINO_GSL_POINTER BoostJsonArray
{
public:
    BoostJsonArray(const boost::json::array &a)
        : a(std::addressof(a))
    {
    }
    constexpr BoostJsonArray() = default;

    // NOLINTNEXTLINE(readability-identifier-naming)
    struct const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = BoostJsonValue;

        constexpr const_iterator() = default;

        constexpr BoostJsonValue operator*() const
        {
            return {this->it};
        }

        constexpr auto operator<=>(const const_iterator &other) const = default;
        constexpr BoostJsonValue operator[](difference_type j) const
        {
            return *(*this + j);
        }
        constexpr const_iterator &operator++()
        {
            ++this->it;
            return *this;
        }
        constexpr const_iterator operator++(int)
        {
            const_iterator n = *this;
            ++this->it;
            return n;
        }
        constexpr const_iterator &operator--()
        {
            this->it--;
            return *this;
        }
        constexpr const_iterator operator--(int)
        {
            const_iterator n = *this;
            this->it--;
            return n;
        }
        constexpr const_iterator &operator+=(difference_type j)
        {
            this->it += j;
            return *this;
        }
        constexpr const_iterator &operator-=(difference_type j)
        {
            this->it -= j;
            return *this;
        }
        constexpr const_iterator operator+(difference_type j) const
        {
            const_iterator r = *this;
            return r += j;
        }
        constexpr const_iterator operator-(difference_type j) const
        {
            return this->operator+(-j);
        }
        constexpr difference_type operator-(const_iterator j) const
        {
            return this->it - j.it;
        }

        friend constexpr const_iterator operator+(difference_type j,
                                                  const_iterator it)
        {
            return it.it + j;
        }

    private:
        constexpr const_iterator(boost::json::array::const_iterator it)
            : it(it)
        {
            static_assert(
                std::is_pointer_v<boost::json::array::const_iterator>);
        }

        boost::json::array::const_iterator it = nullptr;
        friend class BoostJsonArray;
    };

    using iterator = const_iterator;

    iterator begin() const
    {
        if (!this->a)
        {
            return {};
        }
        return this->a->begin();
    }
    iterator end() const
    {
        if (!this->a)
        {
            return {};
        }
        return this->a->end();
    }

    bool empty() const
    {
        return this->a == nullptr || this->a->empty();
    }
    size_t size() const
    {
        if (!this->a)
        {
            return 0;
        }
        return this->a->size();
    }

    BoostJsonValue at(size_t i) const;
    BoostJsonValue operator[](size_t i) const
    {
        return this->at(i);
    }

private:
    constexpr BoostJsonArray(const boost::json::array *a)
        : a(a)
    {
    }

    const boost::json::array *a = nullptr;

    friend BoostJsonValue;
};

}  // namespace chatterino
