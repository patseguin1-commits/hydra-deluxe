// The two base classes every background job derives from: the thread +
// cancel/finished lifecycle (JobBase) and the ok/error result surface on top
// of it (ResultJobBase). Read by the job headers next to this one
// (library_jobs.h, preview_load_job.h, dm_jobs.h); the views only ever touch
// the concrete jobs, never these directly.

#ifndef HYDRA_UI_JOB_BASE_H
#define HYDRA_UI_JOB_BASE_H

#include <atomic>
#include <exception>
#include <functional>
#include <string>
#include <thread>

#include "app/user_messages.h"

namespace hydra::ui {

// Thrown by JobBase::throw_if_cancelled() between a job's steps. run_guarded
// turns it into a failed run whose error() reads "cancelled".
struct JobCancelled : std::exception {
    const char* what() const noexcept override { return "cancelled"; }
};

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
    // Stops the job here when cancel() was called. The UI thread joins a
    // cancelled job, so a long job gives up between its steps instead of
    // running to the end while the window waits.
    void throw_if_cancelled() const {
        if (cancel_.load()) throw JobCancelled{};
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
    // The raw exception text, for a small details line. Unchanged from
    // before: a cancelled job still reads "cancelled" here.
    const std::string& error() const { return error_; }
    // What the user reads: app::plain_error of the exception. Empty when the
    // job succeeded or was cancelled, because a cancel is the user's own
    // click and needs no message.
    const std::string& message() const { return message_; }

protected:
    // Runs the job body; f returns whether the job succeeded. Any escaping
    // exception becomes the job's error text and plain message. Always
    // publishes finished.
    template <class F>
    void run_guarded(F&& f) {
        try {
            ok_ = f();
        } catch (const std::exception& e) {
            error_ = e.what();
            if (!is_cancelled()) message_ = app::plain_error(e);
            ok_ = false;
        }
        finished_.store(true);
    }

    bool ok_ = false;
    std::string error_;
    std::string message_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_JOB_BASE_H
