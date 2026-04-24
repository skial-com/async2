#ifndef ASYNC2_TSAN_SCENARIOS_H
#define ASYNC2_TSAN_SCENARIOS_H

#include <string>

// Each scenario returns 0 on success (work was driven + invariants held) and
// non-zero if the harness itself detected an invariant violation. TSan race
// reports are written to stderr independently and do not set this return.
struct ScenarioResult {
    int exit_code;
    std::string summary;  // one-line counter summary, printed on success
};

ScenarioResult RunLockedQueue(int seconds);
ScenarioResult RunDataNodeRefcount(int seconds);
ScenarioResult RunDataNodePool(int seconds);

#endif
