#include <cstdint>
#include <cstdio>

#include <mpmc/boundRingQueue.hpp>

#include "check.hpp"

static void bound_single_thread()
{
    mpmc::boundRingQueue<int, 4> q;
    CHECK(!q.pop());
    for (int i = 0; i < 4; ++i)
    {
        CHECK(q.push(i));
    }
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
        mpmc::boundRingQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
        {
            CHECK(q.emplace(i));
        }
        auto v = q.pop();
        CHECK(v && *v->p == 0);
    }
    CHECK(g_alive.load() == 0);
}

int main()
{
    bound_single_thread();
    bound_move_only_and_destruction();
    stressMultiConsumer<mpmc::boundRingQueue<std::uint64_t, 1024>>(3, 3, 200'000);
    stressMultiConsumer<mpmc::boundRingQueue<std::uint64_t, 1024>>(4, 1, 200'000);
    stressMultiConsumer<mpmc::boundRingQueue<std::uint64_t, 1024>>(1, 4, 300'000);
    std::puts("mpmc: all tests passed");
}
