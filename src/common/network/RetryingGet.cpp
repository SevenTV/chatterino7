#include "common/network/RetryingGet.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

namespace {

using namespace chatterino;

constexpr uint8_t MAX_TRIES = 16;

void tryRequest(const QUrl &url, std::function<void(NetworkResult)> onSuccess,
                std::function<void(NetworkResult)> onError, int timeout,
                uint8_t tries)
{
    auto req = NetworkRequest(url).onSuccess(onSuccess).onError(
        [url, timeout, tries, onSuccess = std::move(onSuccess),
         onError = std::move(onError)](NetworkResult result) mutable {
            if (result.status() != 429 || tries >= MAX_TRIES)
            {
                onError(std::move(result));
                return;
            }

            int nextTry = tries + 1;
            std::chrono::milliseconds delay{
                (1 << (nextTry + 2)) *
                (1000 + QRandomGenerator::global()->bounded(1000))};

            QTimer::singleShot(
                delay, [url, timeout, onSuccess = std::move(onSuccess),
                        onError = std::move(onError), nextTry] mutable {
                    if (!tryGetApp() || isAppAboutToQuit())
                    {
                        return;
                    }
                    tryRequest(url, std::move(onSuccess), std::move(onError),
                               timeout, nextTry);
                });
        });

    if (timeout > 0)
    {
        req = std::move(req).timeout(timeout);
    }
    req.execute();
}

}  // namespace

namespace chatterino::network {

void fetchWithBackoff(const QUrl &url,
                      std::function<void(NetworkResult)> onSuccess,
                      std::function<void(NetworkResult)> onError, int timeout)
{
    tryRequest(url, std::move(onSuccess), std::move(onError), timeout, 0);
}

}  // namespace chatterino::network
