// Hardware-free regression test for the discovery poll logic (poll.hpp).
//
// Reproduces the reported bug scenario: the "device list" is empty at first and
// only publishes the device after a delay (mirroring the SDK reading identity,
// then emitting "add uvc device" a moment later). The old single-sample check
// could miss that window; poll_until_resolved must not.
//
// Build/run:  make test   (from gui/)

#include "poll.hpp"

#include <cassert>
#include <cstdio>
#include <memory>

int main() {
    // Case 1: device appears ~1000 ms in. Must be resolved promptly (well before
    // a 6000 ms timeout), proving the poller keeps sampling past the empty window.
    {
        const auto start = std::chrono::steady_clock::now();
        std::function<std::shared_ptr<int>()> probe = [start]() -> std::shared_ptr<int> {
            int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            return elapsed >= 1000 ? std::make_shared<int>(42) : nullptr;
        };
        auto r = poll_until_resolved<std::shared_ptr<int>>(probe, 6000, 100,
                                                           [](int) {});
        int took = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        assert(r && *r == 42 && "late device must be resolved");
        assert(took >= 1000 && took < 2000 && "must resolve soon after it appears");
        printf("case1 PASS: resolved late-published device at ~%d ms (timeout was 6000)\n", took);
    }

    // Case 2: device never appears. Must return null at ~timeout, not earlier.
    {
        const auto start = std::chrono::steady_clock::now();
        std::function<std::shared_ptr<int>()> probe = []() -> std::shared_ptr<int> {
            return nullptr;
        };
        auto r = poll_until_resolved<std::shared_ptr<int>>(probe, 800, 100,
                                                           [](int) {});
        int took = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        assert(!r && "no device -> null");
        assert(took >= 800 && took < 1500 && "must wait the full timeout before giving up");
        printf("case2 PASS: timed out with no device at ~%d ms (timeout was 800)\n", took);
    }

    printf("ALL POLL LOGIC TESTS PASS\n");
    return 0;
}
