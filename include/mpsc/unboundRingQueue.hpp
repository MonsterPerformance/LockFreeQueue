#pragma once
#include <atomic>
#include <concepts>
#include <expected>
#include <optional>
#include <utility>

#include <common/common.hpp>

namespace mpsc
{

// Unbounded multi-producer / single-consumer linked queue (Vyukov intrusive MPSC).
// One allocation per push; push is wait-free (a single exchange) and never fails.
// Exactly one thread may pop. pop() can report "empty" for a moment while a producer
// sits between its exchange and publishing the link; the element shows up on a later call.
template <std::move_constructible T>
class unboundRingQueue
{
    struct Node
    {
        std::atomic<Node*> next{nullptr};
        std::optional<T> value;

        Node() = default;

        explicit Node(T&& v) : value(std::in_place, std::move(v))
        {
        }
    };

public:
    unboundRingQueue()
    {
        auto* stub = new Node();
        head_.store(stub, std::memory_order_relaxed);
        tail_ = stub;
    }

    ~unboundRingQueue()
    {
        while (pop())
        {
        }
        delete tail_;
    }

    unboundRingQueue(const unboundRingQueue&) = delete;
    unboundRingQueue& operator=(const unboundRingQueue&) = delete;

    void push(T v)
    {
        auto* n = new Node(std::move(v));
        auto* prev = head_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    // Consumer side only.
    [[nodiscard]] std::expected<T, lfq::QueueEmpty> pop()
    {
        auto* t = tail_;
        auto* next = t->next.load(std::memory_order_acquire);
        if (!next) [[unlikely]]
        {
            return std::unexpected(lfq::QueueEmpty{});
        }
        std::expected<T, lfq::QueueEmpty> result(std::in_place, std::move(*next->value));
        next->value.reset();
        tail_ = next;
        delete t;
        return result;
    }

private:
    alignas(lfq::kCacheLine) std::atomic<Node*> head_;
    alignas(lfq::kCacheLine) Node* tail_;
};

}
