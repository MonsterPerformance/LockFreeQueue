#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include <spsc/boundRingQueue.hpp>
#include <spsc/unboundRingQueue.hpp>

#include "check.hpp"

static void bound_single_thread()
{
    spsc::boundRingQueue<int, 4> q;
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
        spsc::boundRingQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
        {
            CHECK(q.emplace(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

static void bound_drain()
{
    spsc::boundRingQueue<int, 8> q;
    for (int i = 0; i < 6; ++i)
    {
        CHECK(q.push(i));
    }
    int sum = 0;
    CHECK(q.drain([&](int&& x) { sum += x; }, 4) == 4);
    CHECK(sum == 0 + 1 + 2 + 3);
    CHECK(q.drain([&](int&& x) { sum += x; }) == 2);
    CHECK(sum == 15);
    CHECK(q.empty());
    CHECK(q.drain([&](int&&) { CHECK(false); }) == 0);
}

static void bound_drain_exception_safety()
{
    {
        spsc::boundRingQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
        {
            CHECK(q.emplace(i));
        }
        int seen = 0;
        bool thrown = false;
        try
        {
            q.drain([&](Tracked&&)
            {
                if (++seen == 3)
                {
                    throw 1;
                }
            });
        }
        catch (int)
        {
            thrown = true;
        }
        CHECK(thrown && seen == 3);
        CHECK(g_alive.load() == 2);  // the three handled elements are gone, two remain queued
        auto v = q.pop();
        CHECK(v && *v->p == 3);
    }
    CHECK(g_alive.load() == 0);
}

static void bound_threaded()
{
    constexpr std::uint64_t kCount = 1'000'000;
    spsc::boundRingQueue<std::uint64_t, 1024> q;
    std::jthread producer([&]
    {
        for (std::uint64_t i = 0; i < kCount; ++i)
        {
            while (!q.push(i))
            {
                std::this_thread::yield();
            }
        }
    });
    for (std::uint64_t next = 0; next < kCount;)
    {
        auto r = q.pop();
        if (!r)
        {
            continue;
        }
        CHECK(*r == next);
        ++next;
    }
    CHECK(!q.pop());
}

static void unbound_single_thread()
{
    spsc::unboundRingQueue<std::string> q;
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
}

static void unbound_destruction()
{
    {
        spsc::unboundRingQueue<Tracked> q;
        for (int i = 0; i < 5; ++i)
        {
            q.push(Tracked(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

static void unbound_threaded()
{
    constexpr std::uint64_t kCount = 500'000;
    spsc::unboundRingQueue<std::uint64_t> q;
    std::jthread producer([&]
    {
        for (std::uint64_t i = 0; i < kCount; ++i)
        {
            q.push(i);
        }
    });
    for (std::uint64_t next = 0; next < kCount;)
    {
        auto r = q.pop();
        if (!r)
        {
            continue;
        }
        CHECK(*r == next);
        ++next;
    }
}

int main()
{
    bound_single_thread();
    bound_move_only_and_destruction();
    bound_drain();
    bound_drain_exception_safety();
    bound_threaded();
    unbound_single_thread();
    unbound_destruction();
    unbound_threaded();
    std::puts("spsc: all tests passed");
}
