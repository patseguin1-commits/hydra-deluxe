// A small thread pool shared by the library scan (hashing chart files) and
// the batch runner (analyzing charts).
//
// Picture a kitchen pass. Cooks (worker threads) each take the next ticket,
// cook it, and set the plate on the pass. One runner (the calling thread)
// carries plates out in the order they land. When the kitchen closes early,
// the last cook to leave rings the bell, so the runner never stands at an
// empty pass waiting for a plate nobody is cooking.

#ifndef HYDRA_APP_WORK_POOL_H
#define HYDRA_APP_WORK_POOL_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hydra::app {

// Runs work(i) for every i in [0, count) on up to `worker_count` threads, and
// hands each result to consume(Result&&) on the calling thread in the order
// the results finish. consume never runs on a worker, so it may write the
// store or touch UI state.
//
// Once *cancel is set, workers stop taking new items. Items already running
// finish (work decides how fast) and still reach consume, so consume sees
// every item that was started and nothing else. The call returns once every
// worker has left. The last one out wakes the consumer while holding the
// lock, so the wake-up cannot slip in between the consumer's check and its
// wait.
//
// If consume throws, workers stop taking items, the pool is joined, and the
// exception propagates. work must not throw: catch inside it. Result must be
// default-constructible and movable.
template <typename Result, typename Work, typename Consume>
void run_work_pool(size_t count, int worker_count, const std::atomic<bool>* cancel,
                   Work&& work, Consume&& consume) {
    if (count == 0) return;

    std::mutex mu;
    std::condition_variable cv;
    std::deque<Result> finished;  // guarded by mu
    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};

    const size_t nworkers =
        std::min(count, static_cast<size_t>(std::max(1, worker_count)));
    size_t live = nworkers;  // workers still running; guarded by mu

    std::vector<std::thread> pool;
    pool.reserve(nworkers);
    for (size_t w = 0; w < nworkers; ++w) {
        pool.emplace_back([&] {
            for (;;) {
                if (stop.load() || (cancel && cancel->load())) break;
                const size_t i = next.fetch_add(1);
                if (i >= count) break;
                Result r = work(i);
                {
                    std::lock_guard<std::mutex> lock(mu);
                    finished.push_back(std::move(r));
                }
                cv.notify_one();
            }
            std::lock_guard<std::mutex> lock(mu);
            if (--live == 0) cv.notify_one();
        });
    }

    try {
        for (;;) {
            Result r;
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait(lock, [&] { return !finished.empty() || live == 0; });
                if (finished.empty()) break;  // every worker has left
                r = std::move(finished.front());
                finished.pop_front();
            }
            consume(std::move(r));
        }
    } catch (...) {
        stop.store(true);
        for (std::thread& t : pool) t.join();
        throw;
    }
    for (std::thread& t : pool) t.join();
}

}  // namespace hydra::app

#endif  // HYDRA_APP_WORK_POOL_H
