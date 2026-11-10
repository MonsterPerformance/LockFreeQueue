# LockFreeQueue

Header-only lock-free queues for C++23, grouped by how many threads push and pop:
**s**ingle / **m**ulti **p**roducer, **s**ingle / **m**ulti **c**onsumer.

| Header | Class | Capacity | Push | Pop |
|---|---|---|---|---|
| `<spsc/boundRingQueue.hpp>` | `spsc::boundRingQueue<T, N>` | fixed, `N` = power of two | wait-free | wait-free |
| `<spsc/unboundRingQueue.hpp>` | `spsc::unboundRingQueue<T>` | unbounded, one allocation per push | wait-free, never fails | wait-free |
| `<spmc/boundRingQueue.hpp>` | `spmc::boundRingQueue<T, N>` | fixed, `N` = power of two | wait-free | lock-free (CAS) |
| `<mpsc/boundRingQueue.hpp>` | `mpsc::boundRingQueue<T, N>` | fixed, `N` = power of two | lock-free (CAS) | wait-free |
| `<mpsc/unboundRingQueue.hpp>` | `mpsc::unboundRingQueue<T>` | unbounded, one allocation per push | wait-free (one exchange), never fails | wait-free |
| `<mpsc/multiRingQueue.hpp>` | `mpsc::multiRingQueue<T, N, P>` | fixed, `N` per producer, up to `P` producers | wait-free, no shared atomic | wait-free |
| `<mpmc/boundRingQueue.hpp>` | `mpmc::boundRingQueue<T, N>` | fixed, `N` = power of two | lock-free (CAS) | lock-free (CAS) |

There is no unbounded SPMC or MPMC queue on purpose: with several consumers, freeing nodes safely needs
a memory-reclamation scheme (hazard pointers, epochs), which this library does not include.

Shared types (`QueueFull`, `QueueEmpty`, `NoFreeProducer`, the cache-line size and the slot storage) live in
`<common/common.hpp>`; each family namespace re-exports them, so `mpsc::QueueFull`, `spsc::QueueEmpty`, ... work.

## Rules

- Thread roles are part of the contract: *single* means exactly one thread may do that operation.
- Order is FIFO. With several producers that means FIFO per producer; `multiRingQueue` has no order across producers.
- Bounded queues return `std::expected<void, QueueFull>` from `push` / `emplace`; every `pop` returns
  `std::expected<T, QueueEmpty>`. Both are `[[nodiscard]]`.
- With several threads on one side, "full" and "empty" can be reported briefly while another thread is in the
  middle of publishing or releasing a slot. Retry; nothing is lost.
- `T` must be move constructible; move-only types are fine. Elements still queued are destroyed with the queue.

## Examples

```cpp
#include <mpsc/boundRingQueue.hpp>

mpsc::boundRingQueue<int, 1024> q;
if (!q.push(42))
{
    // full
}
if (auto v = q.pop())
{
    use(*v);
}
```

`mpsc::multiRingQueue` gives every producer thread its own SPSC ring. Each producer takes a handle with
`acquire()` (at most `P`, handles are never returned) and must be the only user of it. The consumer calls
`pop()` (round-robin) or `drain(fn)` (batch, one head update per ring). The `P * N` elements live inside the
object, so keep `N * P` reasonable on the stack.

```cpp
#include <mpsc/multiRingQueue.hpp>

mpsc::multiRingQueue<int, 1024, 8> q;
auto h = q.acquire();
if (h)
{
    h->push(42);
}
q.drain([](int&& v)
{
    use(v);
});
```

`spsc::boundRingQueue` has the same `drain(fn, limit)`. If `fn` throws, the element it was handed counts as
consumed, the ones already handled stay consumed and the rest stay queued.

More in `examples/`.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Options (all default to `ON` when this is the top-level project, `OFF` when added as a subdirectory):
`-DLOCKFREEQUEUE_BUILD_TESTS=OFF`, `-DLOCKFREEQUEUE_BUILD_EXAMPLES=OFF`, `-DLOCKFREEQUEUE_INSTALL=OFF`.
`-DLOCKFREEQUEUE_SANITIZE_THREAD=ON` (default `OFF`) builds the tests with ThreadSanitizer.

Requires CMake 3.21+ and a C++23 compiler with `<expected>` (GCC 12+, Clang 16+, MSVC 19.33+).

## Use from another project

```cmake
add_subdirectory(LockFreeQueue)
target_link_libraries(app PRIVATE LockFreeQueue::LockFreeQueue)
```

or, after installing:

```cmake
find_package(LockFreeQueue REQUIRED)
target_link_libraries(app PRIVATE LockFreeQueue::LockFreeQueue)
```

## Layout

```
include/common/   common.hpp
include/spsc/     boundRingQueue.hpp  unboundRingQueue.hpp
include/spmc/     boundRingQueue.hpp
include/mpsc/     boundRingQueue.hpp  unboundRingQueue.hpp  multiRingQueue.hpp
include/mpmc/     boundRingQueue.hpp
tests/            one test program per family + check.hpp
examples/
```
