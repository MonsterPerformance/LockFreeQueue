#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <mpsc/boundRingQueue.hpp>
#include <mpsc/multiRingQueue.hpp>
#include <mpsc/unboundRingQueue.hpp>

#include "check.hpp"

static void bound_single_thread()
{
    mpsc::boundRingQueue<int, 4> q;
    CHECK(q.empty());
    CHECK(!q.pop());
    for (int i = 0; i < 4; ++i)
    {
        CHECK(q.push(i));
    }
    CHECK(!q.empty());
    CHECK(!q.push(99));
    for (int i = 0; i < 4; ++i)
    {
        auto v = q.pop();
        CHECK(v && *v == i);
    }
    CHECK(!q.pop());
    for (int i = 0; i < 100; ++i)
    {
        CHECK(q.push(i));
        auto v = q.pop();
        CHECK(v && *v == i);
    }
}

static void bound_move_only_and_destruction()
{
    {
        mpsc::boundRingQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
        {
            CHECK(q.emplace(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

static void bound_multi_producer()
{
    constexpr int P = 4;
    constexpr std::uint64_t PER = 200'000;
    mpsc::boundRingQueue<std::uint64_t, 1024> q;
    std::vector<std::jthread> prod;
    for (int p = 0; p < P; ++p)
    {
        prod.emplace_back([&, p]
        {
            for (std::uint64_t i = 0; i < PER; ++i)
            {
                while (!q.push(encode(p, i)))
                {
                    std::this_thread::yield();
                }
            }
        });
    }
    std::array<std::uint64_t, P> next{};
    for (std::uint64_t got = 0; got < P * PER;)
    {
        auto r = q.pop();
        if (!r)
        {
            continue;
        }
        const auto p = producerOf(*r);
        CHECK(indexOf(*r) == next[p]);
        ++next[p];
        ++got;
    }
    for (auto n : next)
    {
        CHECK(n == PER);
    }
    CHECK(!q.pop());
}

static void unbound_single_thread()
{
    mpsc::unboundRingQueue<std::string> q;
    CHECK(!q.pop());
    for (int i = 0; i < 1000; ++i)
    {
        q.push(std::to_string(i));
    }
    for (int i = 0; i < 1000; ++i)
    {
        auto v = q.pop();
        CHECK(v && *v == std::to_string(i));
    }
    CHECK(!q.pop());
    q.push("left over");
}

static void unbound_destruction()
{
    {
        mpsc::unboundRingQueue<Tracked> q;
        for (int i = 0; i < 5; ++i)
        {
            q.push(Tracked(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

static void unbound_multi_producer()
{
    constexpr int P = 4;
    constexpr std::uint64_t PER = 200'000;
    mpsc::unboundRingQueue<std::uint64_t> q;
    std::vector<std::jthread> prod;
    for (int p = 0; p < P; ++p)
    {
        prod.emplace_back([&, p]
        {
            for (std::uint64_t i = 0; i < PER; ++i)
            {
                q.push(encode(p, i));
            }
        });
    }
    std::array<std::uint64_t, P> next{};
    for (std::uint64_t got = 0; got < P * PER;)
    {
        auto r = q.pop();
        if (!r)
        {
            continue;
        }
        const auto p = producerOf(*r);
        CHECK(indexOf(*r) == next[p]);
        ++next[p];
        ++got;
    }
    for (auto n : next)
    {
        CHECK(n == PER);
    }
    CHECK(!q.pop());
}

static void multi_single_thread()
{
    mpsc::multiRingQueue<int, 4, 2> q;
    CHECK(!q.pop());
    auto a = q.acquire();
    auto b = q.acquire();
    CHECK(a && b);
    CHECK(!q.acquire());
    CHECK(a->push(1));
    CHECK(a->push(2));
    CHECK(b->push(10));
    auto v1 = q.pop();
    CHECK(v1 && *v1 == 1);
    auto v2 = q.pop();
    CHECK(v2 && *v2 == 10);
    auto v3 = q.pop();
    CHECK(v3 && *v3 == 2);
    CHECK(!q.pop());
    for (int i = 0; i < 4; ++i)
    {
        CHECK(a->push(i));
    }
    CHECK(!a->push(99));
    int sum = 0;
    const auto count = q.drain([&](int&& x) { sum += x; });
    CHECK(count == 4);
    CHECK(sum == 6);
    CHECK(!q.pop());
    for (int i = 0; i < 100; ++i)
    {
        CHECK(b->push(i));
        auto v = q.pop();
        CHECK(v && *v == i);
    }
}

static void multi_destruction()
{
    {
        mpsc::multiRingQueue<Tracked, 4, 2> q;
        auto a = q.acquire();
        auto b = q.acquire();
        CHECK(a && b);
        for (int i = 0; i < 3; ++i)
        {
            CHECK(a->emplace(i));
            CHECK(b->emplace(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

static void multi_drain_exception_safety()
{
    {
        mpsc::multiRingQueue<Tracked, 8, 1> q;
        auto a = q.acquire();
        CHECK(a);
        for (int i = 0; i < 5; ++i)
        {
            CHECK(a->emplace(i));
        }
        int seen = 0;
        try
        {
            q.drain([&](Tracked&&)
            {
                if (++seen == 2)
                {
                    throw 1;
                }
            });
        }
        catch (int)
        {
        }
        CHECK(seen == 2);
        CHECK(g_alive.load() == 3);
    }
    CHECK(g_alive.load() == 0);
}

static void multi_multi_producer(bool useDrain)
{
    constexpr int P = 4;
    constexpr std::uint64_t PER = 200'000;
    mpsc::multiRingQueue<std::uint64_t, 1024, P> q;
    std::vector<std::jthread> prod;
    for (int p = 0; p < P; ++p)
    {
        prod.emplace_back([&, p]
        {
            auto h = q.acquire();
            CHECK(h);
            for (std::uint64_t i = 0; i < PER; ++i)
            {
                while (!h->push(encode(p, i)))
                {
                    std::this_thread::yield();
                }
            }
        });
    }
    std::array<std::uint64_t, P> next{};
    auto consume = [&](std::uint64_t&& v)
    {
        const auto p = producerOf(v);
        CHECK(indexOf(v) == next[p]);
        ++next[p];
    };
    std::uint64_t got = 0;
    while (got < P * PER)
    {
        if (useDrain)
        {
            got += q.drain(consume);
        }
        else if (auto r = q.pop())
        {
            consume(std::move(*r));
            ++got;
        }
    }
    for (auto n : next)
    {
        CHECK(n == PER);
    }
    CHECK(!q.pop());
}

int main()
{
    bound_single_thread();
    bound_move_only_and_destruction();
    bound_multi_producer();
    unbound_single_thread();
    unbound_destruction();
    unbound_multi_producer();
    multi_single_thread();
    multi_destruction();
    multi_drain_exception_safety();
    multi_multi_producer(true);
    multi_multi_producer(false);
    std::puts("mpsc: all tests passed");
}
