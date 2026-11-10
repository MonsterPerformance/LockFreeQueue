#include <cstdio>
#include <thread>

#include <spsc/boundRingQueue.hpp>

int main()
{
    spsc::boundRingQueue<int, 64> q;

    std::jthread producer([&]
    {
        for (int i = 0; i < 10; ++i)
        {
            while (!q.push(i))
            {
                std::this_thread::yield();
            }
        }
    });

    for (int got = 0; got < 10;)
    {
        if (auto v = q.pop())
        {
            std::printf("got %d\n", *v);
            ++got;
        }
    }
}
