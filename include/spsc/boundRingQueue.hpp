#pragma once
#include <atomic>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>

#include <common/common.hpp>

namespace spsc
{

// Fixed-capacity single-producer / single-consumer ring. Wait-free, no allocation.
// One thread may push, one (other) thread may pop.
template <std::move_constructible T, std::size_t N>
class boundRingQueue
{
    static_assert(lfq::kValidRingSize<N>, "N must be a power of two, at least 2");
    static constexpr std::size_t kMask{N - 1};

public:
    boundRingQueue() = default;

    ~boundRingQueue()
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            const auto end = tail_.load(std::memory_order_relaxed);
            for (auto h = head_.load(std::memory_order_relaxed); h != end; ++h)
            {
                std::destroy_at(&cells_[h & kMask].value);
            }
        }
    }

    boundRingQueue(const boundRingQueue&) = delete;
    boundRingQueue& operator=(const boundRingQueue&) = delete;

    template <class... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] std::expected<void, lfq::QueueFull> emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
    {
        const auto t = tail_.load(std::memory_order_relaxed);
        if (N <= t - cachedHead_)
        {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (N <= t - cachedHead_)
            {
                return std::unexpected(lfq::QueueFull{});
            }
        }
        std::construct_at(&cells_[t & kMask].value, std::forward<Args>(args)...);
        tail_.store(t + 1, std::memory_order_release);
        return {};
    }

    [[nodiscard]] std::expected<void, lfq::QueueFull> push(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return emplace(std::move(v));
    }

    [[nodiscard]] std::expected<T, lfq::QueueEmpty> pop() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == cachedTail_)
        {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (h == cachedTail_)
            {
                return std::unexpected(lfq::QueueEmpty{});
            }
        }
        auto& c = cells_[h & kMask];
        std::expected<T, lfq::QueueEmpty> result(std::in_place, std::move(c.value));
        std::destroy_at(&c.value);
        head_.store(h + 1, std::memory_order_release);
        return result;
    }

    // Consumer side: hands up to `limit` elements to fn(T&&) and publishes the new head once.
    // If fn throws, the element it was given counts as consumed (it is destroyed), the
    // elements already handled stay consumed, and the rest remain in the queue.
    template <class Fn>
        requires std::invocable<Fn&, T&&>
    std::size_t drain(Fn&& fn, std::size_t limit = N)
    {
        const auto h = head_.load(std::memory_order_relaxed);
        cachedTail_ = tail_.load(std::memory_order_acquire);
        auto count = cachedTail_ - h;
        if (limit < count)
        {
            count = limit;
        }

        struct Commit
        {
            std::atomic<std::size_t>& head;
            std::size_t base;
            std::size_t done{0};

            ~Commit()
            {
                if (done != 0)
                {
                    head.store(base + done, std::memory_order_release);
                }
            }
        } commit{head_, h};

        struct Destroy
        {
            T* p;

            ~Destroy()
            {
                std::destroy_at(p);
            }
        };

        for (std::size_t i = 0; i < count; ++i)
        {
            auto& c = cells_[(h + i) & kMask];
            Destroy d{&c.value};
            ++commit.done;
            fn(std::move(c.value));
        }
        return count;
    }

    // Consumer side only.
    [[nodiscard]] bool empty() const noexcept
    {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire);
    }

private:
    alignas(lfq::kCacheLine) std::atomic<std::size_t> tail_{0};
    std::size_t cachedHead_{0};
    alignas(lfq::kCacheLine) std::atomic<std::size_t> head_{0};
    std::size_t cachedTail_{0};
    alignas(lfq::kCacheLine) lfq::detail::Cell<T> cells_[N];
};

}
