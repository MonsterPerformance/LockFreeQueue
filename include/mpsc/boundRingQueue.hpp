#pragma once
#include <atomic>
#include <concepts>
#include <cstddef>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>

#include <common/common.hpp>

namespace mpsc
{

// Fixed-capacity multi-producer / single-consumer ring (Vyukov bounded queue).
// Producers are lock-free (one CAS), exactly one thread may pop.
template <std::move_constructible T, std::size_t N>
class boundRingQueue
{
    static_assert(lfq::kValidRingSize<N>, "N must be a power of two, at least 2");
    static constexpr std::size_t kMask{N - 1};
    using Slot = lfq::detail::Slot<T>;

public:
    boundRingQueue() noexcept
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    ~boundRingQueue()
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            while (pop())
            {
            }
        }
    }

    boundRingQueue(const boundRingQueue&) = delete;
    boundRingQueue& operator=(const boundRingQueue&) = delete;

    template <class... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] std::expected<void, lfq::QueueFull> emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
    {
        auto pos = tail_.load(std::memory_order_relaxed);
        Slot* s;
        for (;;)
        {
            s = &slots_[pos & kMask];
            const auto seq = s->seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq - pos);
            if (diff == 0)
            {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    break;
                }
            }
            else if (diff < 0) [[unlikely]]
            {
                return std::unexpected(lfq::QueueFull{});
            }
            else
            {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
        std::construct_at(&s->value, std::forward<Args>(args)...);
        s->seq.store(pos + 1, std::memory_order_release);
        return {};
    }

    [[nodiscard]] std::expected<void, lfq::QueueFull> push(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return emplace(std::move(v));
    }

    // Consumer side only.
    [[nodiscard]] std::expected<T, lfq::QueueEmpty> pop() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        const auto pos = head_;
        auto& s = slots_[pos & kMask];
        if (s.seq.load(std::memory_order_acquire) != pos + 1) [[unlikely]]
        {
            return std::unexpected(lfq::QueueEmpty{});
        }
        std::expected<T, lfq::QueueEmpty> result(std::in_place, std::move(s.value));
        std::destroy_at(&s.value);
        s.seq.store(pos + N, std::memory_order_release);
        head_ = pos + 1;
        return result;
    }

    // Consumer side only.
    [[nodiscard]] bool empty() const noexcept
    {
        return slots_[head_ & kMask].seq.load(std::memory_order_acquire) != head_ + 1;
    }

private:
    alignas(lfq::kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(lfq::kCacheLine) std::size_t head_{0};
    alignas(lfq::kCacheLine) Slot slots_[N];
};

}
