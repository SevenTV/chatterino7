#pragma once

#include <functional>

class QUrl;

namespace chatterino {

class NetworkResult;

namespace network {

/// Fetch `url` with backoff when 429 (Too Many Requests) is encountered.
void fetchWithBackoff(const QUrl &url,
                      std::function<void(NetworkResult)> onSuccess,
                      std::function<void(NetworkResult)> onError,
                      int timeout = 0);

}  // namespace network

}  // namespace chatterino
