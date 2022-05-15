#include "messages/layouts/MessageLayoutElement.hpp"
#include <qcolor.h>

#include "Application.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchEmotes.hpp"
#include "singletons/Theme.hpp"
#include "util/DebugCount.hpp"

#include <QDebug>
#include <QPainter>

#include <algorithm>
#include <iterator>
#include <optional>
#include <unordered_map>

namespace chatterino {

const QRect &MessageLayoutElement::getRect() const
{
    return this->rect_;
}

MessageLayoutElement::MessageLayoutElement(MessageElement &creator,
                                           const QSize &size)
    : creator_(creator)
{
    this->rect_.setSize(size);
    DebugCount::increase("message layout elements");
}

MessageLayoutElement::~MessageLayoutElement()
{
    DebugCount::decrease("message layout elements");
}

MessageElement &MessageLayoutElement::getCreator() const
{
    return this->creator_;
}

void MessageLayoutElement::setPosition(QPoint point)
{
    this->rect_.moveTopLeft(point);
}

bool MessageLayoutElement::hasTrailingSpace() const
{
    return this->trailingSpace;
}

int MessageLayoutElement::getLine() const
{
    return this->line_;
}

void MessageLayoutElement::setLine(int line)
{
    this->line_ = line;
}

MessageLayoutElement *MessageLayoutElement::setTrailingSpace(bool value)
{
    this->trailingSpace = value;

    return this;
}

MessageLayoutElement *MessageLayoutElement::setLink(const Link &_link)
{
    this->link_ = _link;
    return this;
}

MessageLayoutElement *MessageLayoutElement::setText(const QString &_text)
{
    this->text_ = _text;
    return this;
}

const Link &MessageLayoutElement::getLink() const
{
    return this->link_;
}

const QString &MessageLayoutElement::getText() const
{
    return this->text_;
}

FlagsEnum<MessageElementFlag> MessageLayoutElement::getFlags() const
{
    return this->creator_.getFlags();
}

//
// IMAGE
//

ImageLayoutElement::ImageLayoutElement(MessageElement &creator, ImagePtr image,
                                       const QSize &size)
    : MessageLayoutElement(creator, size)
    , image_(std::move(image))
{
    this->trailingSpace = creator.hasTrailingSpace();
}

void ImageLayoutElement::addCopyTextToString(QString &str, int from,
                                             int to) const
{
    const auto *emoteElement =
        dynamic_cast<EmoteElement *>(&this->getCreator());
    if (emoteElement)
    {
        str += emoteElement->getEmote()->getCopyString();
        str = TwitchEmotes::cleanUpEmoteCode(str);
        if (this->hasTrailingSpace())
        {
            str += " ";
        }
    }
}

int ImageLayoutElement::getSelectionIndexCount() const
{
    return this->trailingSpace ? 2 : 1;
}

void ImageLayoutElement::paint(QPainter &painter)
{
    if (this->image_ == nullptr)
    {
        return;
    }

    auto pixmap = this->image_->pixmapOrLoad();
    if (pixmap && !this->image_->animated())
    {
        // fourtf: make it use qreal values
        painter.drawPixmap(QRectF(this->getRect()), *pixmap, QRectF());
    }
}

void ImageLayoutElement::paintAnimated(QPainter &painter, int yOffset)
{
    if (this->image_ == nullptr)
    {
        return;
    }

    if (this->image_->animated())
    {
        if (auto pixmap = this->image_->pixmapOrLoad())
        {
            auto rect = this->getRect();
            rect.moveTop(rect.y() + yOffset);
            painter.drawPixmap(QRectF(rect), *pixmap, QRectF());
        }
    }
}

int ImageLayoutElement::getMouseOverIndex(const QPoint &abs) const
{
    return 0;
}

int ImageLayoutElement::getXFromIndex(int index)
{
    if (index <= 0)
    {
        return this->getRect().left();
    }
    else if (index == 1)
    {
        // fourtf: remove space width
        return this->getRect().right();
    }
    else
    {
        return this->getRect().right();
    }
}

//
// IMAGE WITH BACKGROUND
//
ImageWithBackgroundLayoutElement::ImageWithBackgroundLayoutElement(
    MessageElement &creator, ImagePtr image, const QSize &size, QColor color)
    : ImageLayoutElement(creator, image, size)
    , color_(color)
{
}

void ImageWithBackgroundLayoutElement::paint(QPainter &painter)
{
    if (this->image_ == nullptr)
    {
        return;
    }

    auto pixmap = this->image_->pixmapOrLoad();
    if (pixmap && !this->image_->animated())
    {
        painter.fillRect(QRectF(this->getRect()), this->color_);

        // fourtf: make it use qreal values
        painter.drawPixmap(QRectF(this->getRect()), *pixmap, QRectF());
    }
}

//
// TEXT
//

TextLayoutElement::TextLayoutElement(MessageElement &_creator, QString &_text,
                                     const QSize &_size, QColor _color,
                                     FontStyle _style, float _scale)
    : MessageLayoutElement(_creator, _size)
    , color_(_color)
    , style_(_style)
    , scale_(_scale)
{
    this->setText(_text);
}

void TextLayoutElement::listenToLinkChanges()
{
    this->managedConnections_.managedConnect(
        static_cast<TextElement &>(this->getCreator()).linkChanged, [this]() {
            this->setLink(this->getCreator().getLink());
        });
}

void TextLayoutElement::addCopyTextToString(QString &str, int from,
                                            int to) const
{
    str += this->getText().mid(from, to - from);

    if (this->hasTrailingSpace())
    {
        str += " ";
    }
}

int TextLayoutElement::getSelectionIndexCount() const
{
    return this->getText().length() + (this->trailingSpace ? 1 : 0);
}

template <typename T, typename M = float>
constexpr inline static T lerp(T a, T b, M t)
{
    return a + (b - a) * t;
}

struct SevenTVGradient {
    std::string function{"linear-gradient"};
    std::optional<ColorUnion> color{{0}};
    std::vector<std::pair<float, ColorUnion>> stops{};
    bool repeat{false};
    float angle{0.0f};

    [[nodiscard]] inline QPen asPen(ColorUnion const &userColor,
                                    bool overlay = false) const
    {
        if (function != "linear-gradient")
            throw std::runtime_error(
                "Only linear-gradient is supported for now.");

        constexpr float pi = 3.141592653589793238462643383279502884f;
        float cosRotation{std::cos(angle * pi / 180.0f)};
        float sinRotation{std::sin(angle * pi / 180.0f)};

        auto scale{repeat ? 0.25f : 1.0f};
        QLinearGradient gradient{
            QPointF{std::clamp(sinRotation, -1.0f, 0.0f) * -1.0f,
                    std::clamp(cosRotation, -1.0f, 0.0f) * -1.0f} *
                scale,
            QPointF{std::clamp(sinRotation, 0.0f, 1.0f),
                    std::clamp(cosRotation, 0.0f, 1.0f)} *
                scale,
        };

        gradient.setCoordinateMode(QGradient::ObjectMode);
        gradient.setSpread(repeat ? QGradient::Spread::RepeatSpread
                                  : QGradient::Spread::PadSpread);

        auto baseColor{color.value_or(userColor)};
        for (auto const &[t, c] : stops)
            gradient.setColorAt(t, overlay ? c : baseColor.mix(c));

        QBrush brush{gradient};

        QPen pen{};
        pen.setBrush(brush);
        return pen;
    }
};

std::unordered_map<std::string, SevenTVGradient> demoGradients{
    {
        "Sunrise",
        {
            .function = "linear-gradient",
            .color = {{-1441816321}},
            .stops =
                {
                    {0.0f, {-5610241}},
                    {0.5f, {-1608944641}},
                    {1.0f, {1175096575}},
                },
            .repeat = false,
            .angle = 90.0f,
        },
    },

    {
        "Metallic",
        {
            .function = "linear-gradient",
            .color = std::nullopt,
            .stops =
                {
                    {0.01f, {2105376127}},
                    {0.15f, {-421075301}},
                    {0.3f, {-1330597761}},
                },
            .repeat = true,
            .angle = 45.0f,
        },
    },

    {
        "Warm Winds",
        {
            .function = "linear-gradient",
            .color = {{-16734977}},
            .stops =
                {
                    {0.0f, {690960127}},
                    {0.3f, {1322108159}},
                    {0.5f, {-134219777}},
                    {0.7f, {-9737217}},
                    {0.99f, {-1675777}},
                },
            .repeat = false,
            .angle = 90.0f,
        },
    },

    {
        "Egg Hunt",
        {
            .function = "linear-gradient",
            .color = std::nullopt,
            .stops =
                {
                    {0.0f, {-172425217}},
                    {0.36f, {-1442841601}},
                    {0.71f, {-2130935809}},
                    {0.98f, {-83902721}},
                },
            .repeat = false,
            .angle = 90.0f,
        },
    },

    {
        "Bubblegum",
        {
            .function = "linear-gradient",
            .color = std::nullopt,
            .stops =
                {
                    {0.01f, {64}},
                    {0.25f, {1347440704}},
                    {0.25f, {-2105376192}},
                    {0.35f, {-926365632}},
                    {0.35f, {-13323265}},
                    {0.75f, {-13323265}},
                    {0.75f, {1450243583}},
                    {0.99f, {1450243583}},
                },
            .repeat = false,
            .angle = 65.0f,
        },
    },

    {
        "Anniversary",
        {
            .function = "linear-gradient",
            .color = {{276859903}},
            .stops =
                {
                    {0.0f, {16774655}},
                    {0.25f, {-1426326529}},
                    {0.5f, {1289014527}},
                    {0.75f, {77314815}},
                    {0.99f, {276859903}},
                },
            .repeat = false,
            .angle = 90.0f,
        },
    },
};

void TextLayoutElement::paint(QPainter &painter)
{
    auto app = getApp();

    painter.setPen(this->color_);

    painter.setFont(app->fonts->getFont(this->style_, this->scale_));

    // Grab a 'unique' gradient for demonstration
    auto gradientIterator{demoGradients.begin()};
    std::advance(gradientIterator,
                 (this->getText().length() > 0)
                     ? (this->getText().toStdString()[0] % demoGradients.size())
                     : 0);
    auto const &gradient{(*gradientIterator).second};

    auto userColor{ColorUnion::fromQColor(color_)};
    if (this->getLink().type == chatterino::Link::UserInfo)
        painter.setPen(gradient.asPen(userColor));

    painter.drawText(
        QRectF(this->getRect().x(), this->getRect().y(), 10000, 10000),
        this->getText(), QTextOption(Qt::AlignLeft | Qt::AlignTop));
}

void TextLayoutElement::paintAnimated(QPainter &, int)
{
}

int TextLayoutElement::getMouseOverIndex(const QPoint &abs) const
{
    if (abs.x() < this->getRect().left())
    {
        return 0;
    }

    auto app = getApp();

    auto metrics = app->fonts->getFontMetrics(this->style_, this->scale_);
    auto x = this->getRect().left();

    for (auto i = 0; i < this->getText().size(); i++)
    {
        auto &&text = this->getText();
        auto width = metrics.horizontalAdvance(this->getText()[i]);

        if (x + width > abs.x())
        {
            if (text.size() > i + 1 && QChar::isLowSurrogate(text[i].unicode()))
            {
                i++;
            }

            return i;
        }

        x += width;
    }

    //    if (this->hasTrailingSpace() && abs.x() < this->getRect().right())
    //    {
    //        return this->getSelectionIndexCount() - 1;
    //    }

    return this->getSelectionIndexCount() - (this->hasTrailingSpace() ? 1 : 0);
}

int TextLayoutElement::getXFromIndex(int index)
{
    auto app = getApp();

    QFontMetrics metrics =
        app->fonts->getFontMetrics(this->style_, this->scale_);

    if (index <= 0)
    {
        return this->getRect().left();
    }
    else if (index < this->getText().size())
    {
        int x = 0;
        for (int i = 0; i < index; i++)
        {
            x += metrics.horizontalAdvance(this->getText()[i]);
        }
        return x + this->getRect().left();
    }
    else
    {
        return this->getRect().right();
    }
}

// TEXT ICON
TextIconLayoutElement::TextIconLayoutElement(MessageElement &creator,
                                             const QString &_line1,
                                             const QString &_line2,
                                             float _scale, const QSize &size)
    : MessageLayoutElement(creator, size)
    , scale(_scale)
    , line1(_line1)
    , line2(_line2)
{
}

void TextIconLayoutElement::addCopyTextToString(QString &str, int from,
                                                int to) const
{
}

int TextIconLayoutElement::getSelectionIndexCount() const
{
    return this->trailingSpace ? 2 : 1;
}

void TextIconLayoutElement::paint(QPainter &painter)
{
    auto app = getApp();

    QFont font = app->fonts->getFont(FontStyle::Tiny, this->scale);

    painter.setPen(app->themes->messages.textColors.system);
    painter.setFont(font);

    QTextOption option;
    option.setAlignment(Qt::AlignHCenter);

    if (this->line2.isEmpty())
    {
        QRect _rect(this->getRect());
        painter.drawText(_rect, this->line1, option);
    }
    else
    {
        painter.drawText(
            QPoint(this->getRect().x(),
                   this->getRect().y() + this->getRect().height() / 2),
            this->line1);
        painter.drawText(QPoint(this->getRect().x(),
                                this->getRect().y() + this->getRect().height()),
                         this->line2);
    }
}

void TextIconLayoutElement::paintAnimated(QPainter &painter, int yOffset)
{
}

int TextIconLayoutElement::getMouseOverIndex(const QPoint &abs) const
{
    return 0;
}

int TextIconLayoutElement::getXFromIndex(int index)
{
    if (index <= 0)
    {
        return this->getRect().left();
    }
    else if (index == 1)
    {
        // fourtf: remove space width
        return this->getRect().right();
    }
    else
    {
        return this->getRect().right();
    }
}

}  // namespace chatterino
