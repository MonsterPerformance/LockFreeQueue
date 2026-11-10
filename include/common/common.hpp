#pragma once
#include <atomic>
#include <bit>
#include <cstddef>

// Everything shared by the queue families (spsc, spmc, mpsc, mpmc).

namespace lfq
{

inline constexpr std::size_t kCacheLine{64};

// Returned (inside std::expected) when a bounded queue has no free slot.
struct QueueFull
{
};

// Returned (inside std::expected) when a queue has nothing to pop.
struct QueueEmpty
{
};

// Returned by multiRingQueue::acquire() when all producer handles are taken.
struct NoFreeProducer
{
};

// Bounded rings index with a mask, so N must be a power of two, at least 2.
template <std::size_t N>
inline constexpr bool kValidRingSize{2 <= N && std::has_single_bit(N)};

namespace detail
{

// Raw storage for one T. The lifetime of `value` is managed by the queue
// (construct_at on push, destroy_at on pop), so T need not be default constructible.
template <class T>
struct Cell
{
    union
    {
        T value;
    };

    Cell() noexcept
    {
    }

    ~Cell()
    {
    }
};

// A Cell with a sequence number (Vyukov-style bounded queue).
// Slot i starts with seq == i. A push at position p expects seq == p and publishes p + 1,
// a pop at position p expects seq == p + 1 and releases p + N.
template <class T>
struct alignas(kCacheLine) Slot
{
    std::atomic<std::size_t> seq;
    union
    {
        T value;
    };

    Slot() noexcept : seq(0)
    {
    }

    ~Slot()
    {
    }
};

}

}

// Each family namespace re-exports the shared types, so user code can write
// mpsc::QueueFull, spsc::QueueEmpty, ... without knowing about lfq.
namespace spsc
{
using lfq::QueueEmpty;
using lfq::QueueFull;
}

namespace spmc
{
using lfq::QueueEmpty;
using lfq::QueueFull;
}

namespace mpsc
{
using lfq::NoFreeProducer;
using lfq::QueueEmpty;
using lfq::QueueFull;
}

namespace mpmc
{
using lfq::QueueEmpty;
using lfq::QueueFull;
}
