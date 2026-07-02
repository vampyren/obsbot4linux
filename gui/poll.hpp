#pragma once
//
// Discovery poll helper, factored out of main.cpp so its timing behaviour can be
// unit-tested without the SDK or hardware (see poll_logic_test.cpp).
//
// Polls `probe` every poll_ms until it returns a non-null pointer (device
// resolved) or timeout_ms elapses. `on_tick(elapsed_ms)` fires ~once per second
// while still waiting. Returns the resolved pointer, or a null Ptr on timeout.
//
// This is what fixes the "false NO DEVICE" race: instead of one fixed sleep +
// one list read, we keep sampling so the device is picked up the moment the SDK
// finishes publishing it.

#include <chrono>
#include <functional>
#include <thread>

template <typename Ptr>
Ptr poll_until_resolved(const std::function<Ptr()> &probe,
                        int timeout_ms, int poll_ms,
                        const std::function<void(int)> &on_tick) {
    const auto start = std::chrono::steady_clock::now();
    int next_tick_ms = 1000;
    for (;;) {
        Ptr r = probe();
        if (r) return r;

        const int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed_ms >= timeout_ms) return Ptr();

        if (elapsed_ms >= next_tick_ms) {
            if (on_tick) on_tick(elapsed_ms);
            next_tick_ms += 1000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }
}
