#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

inline void checkFailed(const char* expr, int line)
{
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    std::exit(1);
}

#define CHECK(c) ((c) ? void() : checkFailed(#c, __LINE__))

// Counts live instances, so tests can prove that queues destroy what they still hold.
inline std::atomic<int> g_alive{0};

struct Tracked
{
    std::unique_ptr<int> p;

    explicit Tracked(int v) : p(std::make_unique<int>(v))
    {
        ++g_alive;
    }

    Tracked(Tracked&& o) noexcept : p(std::move(o.p))
    {
        ++g_alive;
    }

    ~Tracked()
    {
        --g_alive;
    }
};

// Values carry the producer id in the top byte and a per-producer counter below it.
inline constexpr int kProducerShift = 56;

inline std::uint64_t encode(int producer, std::uint64_t i)
{
    return (std::uint64_t(producer) << kProducerShift) | i;
}

inline int producerOf(std::uint64_t v)
{
    return int(v >> kProducerShift);
}

inline std::uint64_t indexOf(std::uint64_t v)
{
    return v & ((1ull << kProducerShift) - 1);
}

// Runs `producers` pushing threads against `consumers` popping threads on a fresh Queue
// (a queue of uint64_t with push()/pop()). Checks that every value arrives exactly once and
// that each consumer sees every producer's values in increasing order.
template <class Queue>
void stressMultiConsumer(int producers, int consumers, std::uint64_t per)
{
    Queue q;
    const std::uint64_t total = std::uint64_t(producers) * per;
    std::vector<std::atomic<std::uint8_t>> seen(total);
    std::atomic<std::uint64_t> consumed{0};
    {
        std::vector<std::jthread> threads;
        for (int p = 0; p < producers; ++p)
        {
            threads.emplace_back([&, p]
            {
                for (std::uint64_t i = 0; i < per; ++i)
                {
                    while (!q.push(encode(p, i)))
                    {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (int c = 0; c < consumers; ++c)
        {
            threads.emplace_back([&]
            {
                std::vector<std::uint64_t> nextMin(producers, 0);
                while (consumed.load(std::memory_order_relaxed) < total)
                {
                    auto r = q.pop();
                    if (!r)
                    {
                        std::this_thread::yield();
                        continue;
                    }
                    const auto p = producerOf(*r);
                    const auto i = indexOf(*r);
                    CHECK(p < producers && i < per);
                    CHECK(nextMin[p] <= i);
                    nextMin[p] = i + 1;
                    CHECK(seen[std::uint64_t(p) * per + i].fetch_add(1) == 0);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    }
    CHECK(consumed.load() == total);
    for (auto& s : seen)
    {
        CHECK(s.load() == 1);
    }
    CHECK(!q.pop());
}
