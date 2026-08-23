// The two base classes every background job derives from: the thread +
// cancel/finished lifecycle (JobBase) and the ok/error result surface on top
// of it (ResultJobBase). Read by the job headers next to this one
// (library_jobs.h, preview_load_job.h, dm_jobs.h); the views only ever touch
// the concrete jobs, never these directly.

#ifndef HYDRA_UI_JOB_BASE_H
#define HYDRA_UI_JOB_BASE_H

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace hydra::ui {

// Common lifecycle for every job: an owning worker thread, a cancel flag the
// worker polls, and a finished flag whose release-store publishes everything
// the worker wrote before it (the render thread's finished() load acquires).
// Derived destructors call shutdown() so the join happens while the derived
// members the worker touches are still alive.
class JobBase {
public:
    void cancel() { cancel_.store(true); }
    bool is_cancelled() const { return cancel_.load(); }
    bool finished() const { return finished_.load(); }

    JobBase(const JobBase&) = delete;
    JobBase& operator=(const JobBase&) = delete;

protected:
    JobBase() = default;
    ~JobBase() = default;  // jobs are held and destroyed by concrete type

    void spawn(std::function<void()> fn) { thread_ = std::thread(std::move(fn)); }
    void shutdown() {
        cancel_.store(true);
        if (thread_.joinable()) thread_.join();
    }

    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
};

// Adds the ok/error result surface and the guarded-run tail shared by the
// jobs that produce one result instead of a mutex-guarded snapshot.
class ResultJobBase : public JobBase {
public:
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

protected:
    // Runs the job body; f returns whether the job succeeded. Any escaping
    // exception becomes the job's error text. Always publishes finished.
    template <class F>
    void run_guarded(F&& f) {
        try {
            ok_ = f();
        } catch (const std::exception& e) {
            error_ = e.what();
            ok_ = false;
        }
        finished_.store(true);
    }

    bool ok_ = false;
    std::string error_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_JOB_BASE_H
