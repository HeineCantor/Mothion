#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace mothion {

// Runs body(0) ... body(count - 1) on all the cores (the calling thread is one of them) and returns when
// they are done. The calls are independent: they run in any order, at the same time, so `body` must not
// touch what another call touches. With `cancel` set to true from outside, the calls not begun are skipped.
template <class Body>
void parallelFor(int count, Body &&body, const std::atomic<bool> *cancel = nullptr)
{
    if (count <= 0)
        return;
    const int cores = std::max(1, int(std::thread::hardware_concurrency()));
    const int threads = std::min(count, cores);
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
            if (cancel && cancel->load())
                return;
            body(i);
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(std::size_t(threads - 1));
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(worker);
    worker();
    for (std::thread &t : pool)
        t.join();
}

} // namespace mothion
