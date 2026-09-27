// Plain-English versions of the errors Hydra's jobs can hit. The raw text of
// an exception ("add_song failed: disk I/O error") is for a small details
// line. What the user reads first is a short message that says what
// happened and what to do. Every job and view that shows an error takes the
// wording from here, so it lives in one place.

#ifndef HYDRA_APP_USER_MESSAGES_H
#define HYDRA_APP_USER_MESSAGES_H

#include <exception>
#include <string>
#include <string_view>

namespace hydra::app {

// What every error Hydra doesn't recognize reads as.
inline constexpr const char* kSomethingWentWrong =
    "Something went wrong. Try again, and if it keeps happening, report it with the "
    "details below.";

// What happened and what to do, in at most two short sentences. Uses the
// exception's type where it says more than its text (a chart-file error
// Hydra has no specific wording for still reads as a chart problem).
std::string plain_error(const std::exception& e);

// The same mapping for an error that arrives as text only. run_batch hands
// its failures to the job as strings, so the batch uses this one.
std::string plain_error_text(std::string_view what);

// The raw text, for a small details line under the plain message.
std::string plain_error_detail(const std::exception& e);

}  // namespace hydra::app

#endif  // HYDRA_APP_USER_MESSAGES_H
