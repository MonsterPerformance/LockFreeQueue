#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include <mpmc/boundRingQueue.hpp>

// Two producers and two consumers share one queue; every item is delivered exactly once.
int main()
{
    mpmc::boundRingQueue<int, 256> q;
    constexpr int kProducers = 2;
    constexpr int kEach = 1000;
    std::atomic<int> consumed{0};
    std::atomic<long> sum{0};

    {
        std::vector<std::jthread> threads;
        for (int p = 0; p < kProducers; ++p)
        {
            threads.emplace_back([&]
            {
                for (int i = 1; i <= kEach; ++i)
                {
                    while (!q.push(i))
                    {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (int c = 0; c < 2; ++c)
        {
            threads.emplace_back([&]
            {
                while (consumed.load() < kProducers * kEach)
                {
                    if (auto v = q.pop())
                    {
                        sum += *v;
                        ++consumed;
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            });
        }
    }
    std::printf("consumed %d items, sum %ld\n", consumed.load(), sum.load());
}
