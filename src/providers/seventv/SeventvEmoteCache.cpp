#include "SeventvEmoteCache.hpp"

namespace chatterino {
WeakImageSet::WeakImageSet(const ImageSet &imageSet)
    : size1x(imageSet.getImage1())
    , size2x(imageSet.getImage2())
    , size3x(imageSet.getImage3())
    , size4x(imageSet.getImage4())
{
}

boost::optional<ImageSet> WeakImageSet::lock() const
{
    auto size1 = this->size1x.lock();
    auto size2 = this->size2x.lock();
    auto size3 = this->size3x.lock();
    auto size4 = this->size4x.lock();
    if (size1 || size2 || size3 || size4)
    {
        return boost::none;
    }
    return ImageSet(size1, size2, size3, size4);
}

EmotePtr SeventvEmoteCache::putEmote(const EmoteId &id, Emote &&emote)
{
    if (auto it = this->imageCache_.find(id); it != this->imageCache_.end())
    {
        this->imageCache_.erase(it);
    }
    auto ptr = std::make_shared<const Emote>(std::move(emote));
    this->emoteCache_[id] = ptr;
    return ptr;
}

EmotePtr SeventvEmoteCache::getEmote(const EmoteId &id)
{
    return this->emoteCache_[id].lock();
}

void SeventvEmoteCache::putImageSet(const EmoteId &id, const ImageSet &set)
{
    this->imageCache_.emplace(id, set);
}

WeakImageSet *SeventvEmoteCache::getImageSet(const EmoteId &id)
{
    if (auto it = this->imageCache_.find(id); it != this->imageCache_.end())
    {
        return &it->second;
    }
    return nullptr;
}
}  // namespace chatterino
