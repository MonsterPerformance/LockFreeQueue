#pragma once
#include <atomic>
#include <concepts>
#include <expected>
#include <optional>
#include <utility>

#include <common/common.hpp>

namespace spsc
{

// Unbounded single-producer / single-consumer linked queue. One allocation per push;
// push never fails and needs no atomic read-modify-write.
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
        head_ = stub;
        tail_ = stub;
    }

    ~unboundRingQueue()
    {
        while (pop())
        {
        }
        delete head_;
    }

    unboundRingQueue(const unboundRingQueue&) = delete;
    unboundRingQueue& operator=(const unboundRingQueue&) = delete;

    void push(T v)
    {
        auto* n = new Node(std::move(v));
        tail_->next.store(n, std::memory_order_release);
        tail_ = n;
    }

    [[nodiscard]] std::expected<T, lfq::QueueEmpty> pop()
    {
        auto* old = head_;
        auto* next = old->next.load(std::memory_order_acquire);
        if (!next) [[unlikely]]
        {
            return std::unexpected(lfq::QueueEmpty{});
        }
        std::expected<T, lfq::QueueEmpty> result(std::in_place, std::move(*next->value));
        next->value.reset();
        head_ = next;
        delete old;
        return result;
    }

private:
    alignas(lfq::kCacheLine) Node* head_;  // consumer: current stub node
    alignas(lfq::kCacheLine) Node* tail_;  // producer: last node
};

}
