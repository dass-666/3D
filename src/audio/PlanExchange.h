#pragma once

// Hands immutable objects from the message thread to the audio thread without
// locks or allocation on the audio thread (single hazard pointer).
// Rules: publish()/collect() on any non-audio thread (serialised by a mutex,
// hosts may restore state off the message thread); acquire()/release() on
// the audio thread only, once per block.

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace ses
{

template <typename T>
class PlanExchange
{
public:
    PlanExchange() = default;
    PlanExchange (const PlanExchange&) = delete;
    PlanExchange& operator= (const PlanExchange&) = delete;

    ~PlanExchange()
    {
        delete published.load();
        for (auto* r : retired)
            delete r;
    }

    void publish (std::unique_ptr<T> next)
    {
        const std::lock_guard<std::mutex> g (writerMutex);
        if (auto* old = published.exchange (next.release()))
            retired.push_back (old);
        collectLocked();
    }

    // Frees retired objects the audio thread can no longer be reading.
    void collect()
    {
        const std::lock_guard<std::mutex> g (writerMutex);
        collectLocked();
    }

    size_t numRetired() const noexcept { return retired.size(); }

    // Audio thread.
    const T* acquire() noexcept
    {
        T* p = nullptr;
        do
        {
            p = published.load();
            inUse.store (p);
        } while (p != published.load());
        return p;
    }

    void release() noexcept { inUse.store (nullptr); }

private:
    void collectLocked()
    {
        const T* hazard = inUse.load();
        const T* current = published.load();
        std::erase_if (retired, [&] (T* r)
        {
            if (r == hazard || r == current)
                return false;
            delete r;
            return true;
        });
    }

    std::mutex writerMutex;
    std::atomic<T*> published { nullptr };
    std::atomic<T*> inUse { nullptr };
    std::vector<T*> retired;
};

} // namespace ses
