
#ifndef CAPIO_BACKEND_ATOMIC_QUEUE_HPP
#define CAPIO_BACKEND_ATOMIC_QUEUE_HPP

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

template <typename T> struct AtomicQueueElement {

    AtomicQueueElement(T message, size_t message_size, const std::string &origin) {
        this->object           = message;
        this->object_size      = message_size;
        this->target_or_source = origin;
    }

    T object;
    size_t object_size = 0;
    std::string target_or_source;
};

template <typename T> class AtomicQueue {
    std::queue<AtomicQueueElement<T>> _queue;
    std::mutex _mutex;
    std::condition_variable _lock_cond;

    bool _shutdown = false;

  public:
    ~AtomicQueue() {
        {
            std::lock_guard lg(_mutex);
            _shutdown = true;
        }
        _lock_cond.notify_all();
    }

    void push(T message, size_t message_size, const std::string &origin) {
        {
            std::lock_guard lg(_mutex);
            if (_shutdown) {
                return;
            }
            _queue.emplace(message, message_size, origin);
        }
        _lock_cond.notify_all();
    }

    AtomicQueueElement<T> pop() {
        std::unique_lock lock(_mutex);
        _lock_cond.wait(lock, [this] { return !_queue.empty() || _shutdown; });
        auto s = std::move(_queue.front());
        _queue.pop();

        return s;
    }

    std::optional<AtomicQueueElement<T>> try_pop() {
        std::lock_guard lg(_mutex);
        if (_queue.empty() || _shutdown) {
            return std::nullopt;
        }

        auto s = std::move(_queue.front());
        _queue.pop();
        return s;
    }
};

#endif // CAPIO_BACKEND_ATOMIC_QUEUE_HPP
