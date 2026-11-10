#pragma once
#include <atomic>
#include <concepts>
#include <cstddef>
#include <expected>
#include <type_traits>
#include <utility>

#include <common/common.hpp>
#include <spsc/boundRingQueue.hpp>

namespace mpsc
{

// Multi-producer / single-consumer queue made of up to P private SPSC rings of capacity N,
// one per producer. Producers never share an atomic, so it scales under contention.
// Order is FIFO per producer only; there is no global order across producers.
// Memory: P * N elements live inside the object, mind the stack for large N * P.
template <std::move_constructible T, std::size_t N, std::size_t P>
class multiRingQueue
{
    static_assert(lfq::kValidRingSize<N>, "N must be a power of two, at least 2");
    static_assert(0 != P, "P must not be zero");
    using Ring = spsc::boundRingQueue<T, N>;

public:
    // Handle owned by one producer thread. It must be used by one thread at a time.
    class Producer
    {
    public:
        Producer(Producer&& other) noexcept : ring_(std::exchange(other.ring_, nullptr))
        {
        }

        Producer& operator=(Producer&& other) noexcept
        {
            ring_ = std::exchange(other.ring_, nullptr);
            return *this;
        }

        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;

        template <class... Args>
            requires std::constructible_from<T, Args...>
        [[nodiscard]] std::expected<void, lfq::QueueFull> emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            return ring_->emplace(std::forward<Args>(args)...);
        }

        [[nodiscard]] std::expected<void, lfq::QueueFull> push(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            return ring_->emplace(std::move(v));
        }

    private:
        friend class multiRingQueue;

        explicit Producer(Ring* ring) noexcept : ring_(ring)
        {
        }

        Ring* ring_;
    };

    multiRingQueue() = default;
    multiRingQueue(const multiRingQueue&) = delete;
    multiRingQueue& operator=(const multiRingQueue&) = delete;

    // Hands out one of the P rings. Handles are never returned.
    [[nodiscard]] std::expected<Producer, lfq::NoFreeProducer> acquire() noexcept
    {
        auto n = registered_.load(std::memory_order_relaxed);
        while (n < P)
        {
            if (registered_.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel))
            {
                return Producer(&rings_[n]);
            }
        }
        return std::unexpected(lfq::NoFreeProducer{});
    }

    // Consumer side only: round-robin over the registered producers.
    [[nodiscard]] std::expected<T, lfq::QueueEmpty> pop() noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        const auto n = registered_.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < n; ++i)
        {
            if (n <= cursor_)
            {
                cursor_ = 0;
            }
            auto r = rings_[cursor_].pop();
            ++cursor_;
            if (r)
            {
                return r;
            }
        }
        return std::unexpected(lfq::QueueEmpty{});
    }

    // Consumer side only: batch pop, one head update per ring. See spsc::boundRingQueue::drain
    // for what happens if fn throws.
    template <class Fn>
        requires std::invocable<Fn&, T&&>
    std::size_t drain(Fn&& fn, std::size_t limitPerRing = N)
    {
        const auto n = registered_.load(std::memory_order_acquire);
        std::size_t total = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            total += rings_[i].drain(fn, limitPerRing);
        }
        return total;
    }

private:
    alignas(lfq::kCacheLine) std::atomic<std::size_t> registered_{0};
    alignas(lfq::kCacheLine) std::size_t cursor_{0};
    alignas(lfq::kCacheLine) Ring rings_[P];
};

}
