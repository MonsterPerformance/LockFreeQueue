#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <mpsc/boundRingQueue.hpp>
#include <mpsc/multiRingQueue.hpp>

// Three producers push into one bounded ring, the main thread is the single consumer.
static void sharedRing()
{
    mpsc::boundRingQueue<std::string, 1024> ring;
    constexpr int kProducers = 3;
    constexpr int kEach = 4;

    std::vector<std::jthread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&, p]
        {
            for (int i = 0; i < kEach; ++i)
            {
                while (!ring.emplace("producer " + std::to_string(p) + " msg " + std::to_string(i)))
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int got = 0; got < kProducers * kEach;)
    {
        if (auto v = ring.pop())
        {
            std::puts(v->c_str());
            ++got;
        }
    }
}

// Same idea with one private ring per producer; the consumer drains them in batches.
static void privateRings()
{
    mpsc::multiRingQueue<int, 256, 2> q;
    {
        std::jthread a([&]
        {
            auto h = q.acquire();
            for (int i = 0; i < 5; ++i)
            {
                while (!h->push(i))
                {
                    std::this_thread::yield();
                }
            }
        });
        std::jthread b([&]
        {
            auto h = q.acquire();
            for (int i = 100; i < 105; ++i)
            {
                while (!h->push(i))
                {
                    std::this_thread::yield();
                }
            }
        });
    }
    q.drain([](int&& v) { std::printf("drained %d\n", v); });
}

int main()
{
    sharedRing();
    privateRings();
}
