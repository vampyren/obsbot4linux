# Code Review — OBSBOT Tiny 3 GUI PoC

High-effort, workflow-backed review of the recent GUI changes (`gui/main.cpp`, `gui/poll.hpp`). Because this repo had no git history at review time, the review ran as a **whole-file** review (5 finder angles → independent adversarial verify), not a diff.

- **Angles:** concurrency, lifetime/memory, SDK correctness & safety, logic/UI, cleanup
- **Funnel:** 18 raw candidates → 17 unique locations → **14 survived** adversarial verification
- **Severities after verification:** none critical/high; all Medium or Low (the finders' "high" items were downgraded on verify). The Medium cluster is dominated by one real theme: **no shutdown/lifecycle teardown** of worker threads and the SDK status callback.

## Findings (ranked most-severe first)

### 1. 🟡 [MEDIUM] `gui/main.cpp:423` — concurrency
*Verdict: **CONFIRMED** · finder severity: high → corrected: medium*

**What:** Detached command/PTZ/zoom worker threads outlive the GTK main loop; closing the window mid-command tears down static globals and the GMainContext while the worker is still running a blocking SDK call, then the worker posts into a dead loop and locks destroyed mutexes.

**Failure scenario:** User clicks 'ptz up' (or zoom/preset/wake). The worker is blocked in gimbalGetAttitudeInfoR/gimbalSetSpeedPositionR (slow gimbal move, hundreds of ms). User closes the window -> 'destroy' fires gtk_main_quit (line 758), gtk_main() returns, main() returns and begins destroying namespace-scope statics (ui, g_dev, g_dev_mtx, g_status_mtx, g_status). The still-running detached thread (spawned at 423, and the same pattern at 467, 497, 520, 536, 584, 610) then calls post_log()->g_idle_add() into the torn-down main context and, in zoom_apply, refresh_zoom_snapshot() locks g_status_mtx after it may be destroyed -> use-after-destruction / crash at shutdown. Nothing joins these threads.

**Verifier reasoning:** The defect is real. Detached command/PTZ/zoom/preset workers (spawned at 423, 467, 497, 520, 536, 569, 584, 610) are never joined and there is no shutdown synchronization: the only 'destroy' handler is gtk_main_quit (758). When the window is closed mid-command, gtk_main() returns (1047), main() returns (1053), and the anonymous-namespace static-duration objects (g_status_mtx, g_preset_mtx, g_status, g_presets, ui) run their destructors during the exit sequence while detached workers may still be blocked in slow SDK calls. Concretely: a Zoom-In worker blocked in cameraGetZoomAbsoluteR/cameraSetZoomAbsoluteR then calls refresh_zoom_snapshot() which at main.cpp:235 does std::lock_guard on the now-destroyed g_status_mtx (and writes g_status) -> use-after-destruction / UB; the same holds for preset_save locking g_preset_mtx at 505. The captured shared_ptr<Device> keeps the device alive, so d-> calls are safe, but that does not protect the destroyed statics. One sub-claim is weaker than stated: g_idle_add posts into GLib's ref-counted default GMainContext which survives after the loop stops, so it merely leaks the source rather than crashing; the load-bearing failure is the destroyed-mutex lock, which is accurate. I could not refute the core mechanism.


### 2. 🟡 [MEDIUM] `gui/main.cpp:610` — lifetime
*Verdict: **PLAUSIBLE** · finder severity: medium → corrected: medium*

**What:** on_face_toggled captures the toggle GtkWidget* `w` by value into a detached worker thread and, after a blocking SDK call (cameraSetFaceFocusR), re-arms it via g_idle_add(idle_set_toggle, new ToggleSet{w,...}); if the top-level window is destroyed while that blocking call is in flight, `w` is a freed widget by the time the idle runs.

**Failure scenario:** User toggles Face focus; cameraSetFaceFocusR blocks (device slow/disconnected, seconds). User closes the window: 'destroy' -> gtk_main_quit frees the whole widget tree including the toggle button. The worker thread returns rc!=OK and calls g_idle_add(idle_set_toggle, new ToggleSet{w,!on}); if the pending source is dispatched during the loop's final drain, idle_set_toggle calls gtk_toggle_button_set_active on the freed `w` -> use-after-free. If it is not drained, the ToggleSet is leaked instead.

**Verifier reasoning:** Real defect: on_face_toggled (gui/main.cpp:610) captures the toggle button as a raw GtkWidget* w into a detached worker, and after the blocking cameraSetFaceFocusR re-arms it via g_idle_add(idle_set_toggle,{w,...}) with no thread join, weak-ref, or drain. window "destroy" is wired straight to gtk_main_quit (line 758), which synchronously frees the widget tree including w. The candidate's specific "final drain" mechanism is wrong — after gtk_main() returns, main() simply returns with no post-loop iteration, so an idle added after the loop quits is never dispatched (harmless leak, which the candidate concedes). BUT the UAF is still reachable through a different, narrow path: GLib's g_main_dispatch processes the entire ready-source batch even after g_main_loop_quit is called mid-batch. If the revert idle is marked ready in the SAME iteration whose check phase also captures the window-close event, the GDK event source (priority 0) dispatches first — freeing w and calling gtk_main_quit — and the lower-priority idle (priority 200) is then dispatched in that same batch, calling gtk_toggle_button_set_active on the freed w → use-after-free. This requires rc!=OK plus a same-iteration timing coincidence, so it is real but race-dependent and shutdown-only (crash at exit); the dominant outcomes are normal dispatch or a benign leak.


### 3. 🟡 [MEDIUM] `gui/main.cpp:1047` — lifetime
*Verdict: **CONFIRMED** · finder severity: medium → corrected: medium*

**What:** Detached command/discovery threads and the SDK status-callback thread are never joined or stopped, and enableDevStatusCallback(true) (L356) is never disabled; after gtk_main() returns, the namespace-global state they touch (ui, g_dev, g_status, g_dev_mtx/g_status_mtx, g_presets) is destroyed, so a still-running thread can dereference destroyed objects.

**Failure scenario:** App exits (window closed or self-test gtk_main_quit) while a command thread is mid cameraSet*/gimbal* call or the SDK fires on_dev_status. main() returns, static-duration globals (g_status_mtx, g_dev shared_ptr, ui) begin destruction. The surviving thread then locks g_status_mtx / reads g_dev / calls post_log touching ui -> data race and use-after-free on destroyed globals during process teardown. on_dev_status also keeps writing g_status because the callback was never disabled.

**Verifier reasoning:** The core defect holds via a concrete path. Globals g_status_mtx/g_status/g_presets/g_preset_mtx are anonymous-namespace static-duration objects (lines 85-108) destroyed after main() returns. enableDevStatusCallback(true) (line 356) is never disabled (grep confirms no disable, no join, no cleanup), so the SDK status-callback thread — which the code's own comment (lines 284-286) confirms is a separate thread — keeps firing on_dev_status, locking g_status_mtx (282) and writing g_status (289) every ~2-3s. In GUI mode, closing the window fires destroy→gtk_main_quit (line 758), so gtk_main() returns (1047) and main returns (1052), beginning static destruction while that thread (and any detached preset worker that locks g_preset_mtx/g_presets at 506/516 after a blocking SDK call) is still live → lock/use of a destroyed std::mutex and objects = use-after-free/UB during teardown. Refutation failed: nothing stops the thread or disables the callback. Minor overstatements in the candidate do not invalidate it: command workers capture a shared_ptr copy of d (so they don't read global g_dev, and keep the Device alive), and post_log does not touch ui directly (it only schedules g_idle_add whose ui callbacks never run). The load-bearing UAF is on the static mutexes/g_status/g_presets, which is real.


### 4. 🟡 [MEDIUM] `gui/main.cpp:477` — sdk-correctness
*Verdict: **CONFIRMED** · finder severity: high → corrected: medium*

**What:** Gimbal moves (PTZ nudge, Center, preset Go) never disable AI tracking first, so when AI Track is ON the SDK keeps the gimbal AI-controlled and the move is ignored/overridden — yet the app logs rc and prints "(ok)", misleading the user.

**Failure scenario:** User enables AI Track (cameraSetAiModeU Human). Per the SDK header (dev.hpp:1935-1958: gimbalRstPosR / gimbalSpeedCtrlR — "If the AI smart tracking is enabled, gimbal is always controlled by AI. The follow command be used to disable AI smart tracking ... cameraSetAiModeU(Device::AiWorkModeNone)"), the device ignores manual gimbal targets while AI owns the gimbal. The user then presses ↑/↓/←/→ (ptz_nudge -> gimbalSetSpeedPositionR, line 477), Center (on_center -> gimbalRstPosR, line 444), or a preset Go (preset_go -> gimbalSetSpeedPositionR, line 523). The camera does not move (or AI immediately re-grabs it), but gimbalSetSpeedPositionR/gimbalRstPosR still return RM_RET_OK=0, so the log shows "pitch X->Y ... rc=0 (ok)". The app claims the move succeeded while the camera visibly did not obey. None of on_center/ptz_nudge/preset_go call cameraSetAiModeU(AiWorkModeNone) (or aiSetTargetSelectR(false)) before the move as the SDK requires.

**Verifier reasoning:** Verified in code and SDK header. on_center (main.cpp:444, gimbalRstPosR), ptz_nudge (477, gimbalSetSpeedPositionR), and preset_go (523, gimbalSetSpeedPositionR) all issue gimbal commands with no prior cameraSetAiModeU(AiWorkModeNone)/aiSetTargetSelectR(false); the only cameraSetAiModeU call is the AI toggle (585), and there is no UI guard, so AI-on + manual-move is reachable. dev.hpp:1935-1939 (gimbalRstPosR) explicitly states that when AI smart tracking is enabled the gimbal is always controlled by AI and must be disabled first via cameraSetAiModeU(AiWorkModeNone) — so at minimum Center silently fails while AI is on. The PTZ/preset path uses gimbalSetSpeedPositionR whose doc (1977-1991) omits the AI warning, so that leg is inferred from the same physical constraint rather than documented, and the 'prints (ok)' aspect depends on the device returning RM_RET_OK when it ignores the command (unspecified in the header). The core defect — manual gimbal control not disabling AI as the SDK requires, so moves are dropped while AI owns the gimbal — is real and traceable. Downgraded to medium: impact is confined to the AI-on + manual-move combination, is non-destructive and recoverable, and the specific misleading-'(ok)' claim is uncertain.


### 5. 🟡 [MEDIUM] `gui/main.cpp:585` — sdk-correctness
*Verdict: **CONFIRMED** · finder severity: medium → corrected: medium*

**What:** AI Track toggle never reverts on command failure, and its status-push resync only runs when ai_mode decodes to a known enum, so a rejected AI command or an undecoded mode (e.g. AiWorkModeSwitching=6) leaves the toggle showing a state the device is not in.

**Failure scenario:** on_ai_toggled (line 585) fires the worker that calls cameraSetAiModeU and only logs rc — unlike on_face_toggled (line 610-614) which schedules idle_set_toggle to revert when rc != RM_RET_OK. If cameraSetAiModeU returns non-zero, the toggle stays visually ON while AI is actually OFF. Recovery depends entirely on the ~2-3s status push, but on_status_refresh only re-syncs the toggle inside `if (ui.ai_toggle && ai != nullptr)` (line 264), and ai is nullptr whenever ai_mode_name() can't decode the value — e.g. the transient AiWorkModeSwitching (6), which is not in the ai_mode_name switch (lines 200-212). During a mode transition or a rejected command the toggle can therefore lie indefinitely rather than reflecting real device state, violating the app's stated "keep the AI toggle honest" contract.

**Verifier reasoning:** All load-bearing claims verified against source. on_ai_toggled (main.cpp:584-588) logs rc but never reverts the toggle on failure, unlike on_face_toggled (610-615) which explicitly schedules idle_set_toggle when rc != RM_RET_OK — the code's own comment (591-592) documents that revert intent. The status resync in on_status_refresh is gated by `if (ui.ai_toggle && ai != nullptr)` (line 264), and ai is nullptr for any mode ai_mode_name (200-212) can't decode. AiWorkModeSwitching=6 is a real, documented transient state (dev.hpp:561, "Mode switching is ongoing") absent from that switch, so during a mode transition the toggle-honesty resync (comment line 263) is skipped. Concrete failure: user enables AI Track, cameraSetAiModeU returns non-zero, toggle stays ON while device is in AiWorkModeNone with no revert; if the subsequent status push reports Switching(6) the resync is skipped, so the toggle keeps lying. Recovery depends entirely on an eventually-decodable SDK status callback (setDevStatusCallbackFunc, lines 355-356), which is not guaranteed prompt. Minor overstatement: for a plain rejected command a later push reporting a decodable mode (e.g. None) would resync, so 'indefinitely' is only strictly true when the device stays in an undecoded state or the callback is edge-triggered — but the core defect (missing failure-revert plus decode-gated resync) holds concretely. Medium severity fits: UI/state-honesty correctness bug, not a crash.


### 6. 🟡 [MEDIUM] `gui/main.cpp:421` — logic-ui
*Verdict: **CONFIRMED** · finder severity: medium → corrected: medium*

**What:** No device-removal/disconnect handling: after unplug g_dev is never cleared and controls stay enabled, so every command is dispatched to a stale Device handle.

**Failure scenario:** User connects, then unplugs the camera. There is no removal callback and set_connected_ui(false) is never re-invoked, so g_dev remains a non-null shared_ptr and all g_dev_controls stay sensitive. run_cmd/ptz_nudge/zoom_apply only guard against null (line 421 etc.), never staleness, so clicking Wake/PTZ/Zoom issues blocking SDK calls on a dead handle — at best logging rc=error every time, at worst blocking/crashing inside the SDK. The pill also keeps showing CONNECTED.

**Verifier reasoning:** Traced the lifecycle: discovery_thread() runs once (main.cpp:1035), sets g_dev, and calls on_discovery_done once. There is no re-discovery loop, no timer, and no SDK device-removal/list-change callback registered (only on_dev_status at line 355, which never clears g_dev). set_connected_ui(false) is only reachable from the initial no-device branch (line 316) and is never re-invoked after a successful connect. After unplug, g_dev stays a non-null shared_ptr (the app keeps the Device alive), so the null-only guards at 421/463/495/514/534/566/582/607 all pass and dispatch real SDK calls on a stale handle; the pill stays CONNECTED and all g_dev_controls stay sensitive. This is a real, code-provable defect. The finding is slightly overstated on worst-case impact — commands run on detached worker threads (line 423), so a blocking/hanging SDK call would stall a throwaway thread rather than freeze the GTK main loop, and a crash is plausible but unproven since the shared_ptr keeps the object alive. The provable impact (no disconnect detection, stale-handle dispatch, misleading CONNECTED state, silently failing commands) fully supports a medium finding.


### 7. 🟡 [MEDIUM] `gui/main.cpp:584` — logic-ui
*Verdict: **CONFIRMED** · finder severity: medium → corrected: medium*

**What:** AI toggle has no revert-on-failure path (unlike face toggle) and is not re-synced when the device reports an undecoded AI mode, so the toggle can persistently diverge from real device state.

**Failure scenario:** on_ai_toggled (584-588) fires the SDK command on a worker thread but never checks rc and never reverts the button, relying entirely on the periodic status push to correct it. If cameraSetAiModeU fails, the toggle shows the user-selected state for the ~2-3s push interval. Worse: in on_status_refresh the resync block (line 264) is gated on `ai != nullptr`, so if the device's ai_mode decodes to Unknown (a value not in ai_mode_name), the toggle is never corrected at all and stays stuck on the user's last click while the STATUS panel shows 'Unknown'. Contrast with on_face_toggled, which correctly reverts via idle_set_toggle on rc != RM_RET_OK.

**Verifier reasoning:** Both mechanical claims hold against the code. on_ai_toggled (main.cpp:584-588) fires cameraSetAiModeU on a worker thread, logs rc, and never reverts the button on failure — unlike its sibling on_face_toggled (614) which reverts via idle_set_toggle on rc != RM_RET_OK. The only correction channel, on_status_refresh, gates the toggle re-sync on `ai != nullptr` (264), where ai = ai_mode_name(s.ai_mode) returns nullptr for any value outside its 8-case switch (200-212). This gap is not hypothetical: the SDK enum (libdev.../dev.hpp:554-565) includes AiWorkModeSwitching=6 ("Mode switching is ongoing", reported by the device during every toggle transition) and AiWorkModeCustomize=15, neither decoded. So during any AI switch the STATUS panel flashes "Unknown" and the toggle re-sync is suppressed; for a transient Switching it self-corrects once the settled mode (None/Human) is reported, but if the device rests in an undecoded mode (e.g. Customize set externally) the toggle persistently diverges while STATUS reads "Unknown" — exactly the described failure. Note the `tracking = (s.ai_mode != AiWorkModeNone)` computation would actually be correct for these undecoded tracking modes, so the ai!=nullptr gate is what needlessly suppresses the sync. Impact is bounded (no crash/data loss; failed-command divergence self-corrects within the push interval when the settled mode is decoded; the persistent case needs an externally-set undecoded mode), consistent with medium.


### 8. ⚪ [LOW] `gui/main.cpp:264` — concurrency
*Verdict: **CONFIRMED** · finder severity: low → corrected: low*

**What:** The periodic status-push handler force-syncs the AI toggle to the device's reported state, which can clobber an in-flight user toggle before the device has applied the command, causing the toggle to flip back (visual/state fight).

**Failure scenario:** User turns 'AI Track' on; on_ai_toggled spawns cameraSetAiModeU (585) asynchronously. Before the device applies it, a status push (~2-3s cadence) arrives with the old ai_mode=None; on_status_refresh (264-269) sets g_syncing_ui and forces the toggle back to off. The UI now contradicts the command just issued; user perceives the toggle as not working / flickering.

**Verifier reasoning:** The race is real and traceable in the code. on_ai_toggled (577-589) issues cameraSetAiModeU on a detached worker thread with nonzero apply latency and no local UI state pinning. on_dev_status (registered at line 355) is a device-driven periodic push that marshals on_status_refresh, which at 264-269 unconditionally force-syncs the toggle to the device-reported ai_mode whenever it decodes. Crucially, the ai != nullptr guard does NOT protect against a stale push: ai_mode_name(AiWorkModeNone) returns "Off" (non-null, line 202), so a push carrying the pre-command state None drives line 267 to set the toggle back to inactive under g_syncing_ui (which also suppresses on_ai_toggled from re-issuing). There is no pending-command suppression/debounce, so a status push arriving in the window between the user click and the device applying the command flips the toggle back, contradicting the command; the next post-apply push flips it on again — a visible state fight/flicker. Severity is correctly low: it is self-correcting and frequency depends on push cadence vs command latency, but the defect path is definite and unconditional.


### 9. ⚪ [LOW] `gui/main.cpp:152` — lifetime
*Verdict: **PLAUSIBLE** · finder severity: low → corrected: low*

**What:** g_idle_add payloads allocated by worker threads (std::string in post_log/idle_log, ToggleSet in idle_set_toggle) are leaked whenever they are queued after the main loop has stopped, since the idle handlers that delete them never run.

**Failure scenario:** Any post_log() or idle_set_toggle re-arm issued by a detached thread after gtk_main_quit has been called (e.g. discovery/self-test tail, or a command finishing during shutdown) allocates `new std::string`/`new ToggleSet` and hands it to g_idle_add; with the loop no longer iterating, idle_log/idle_set_toggle are never invoked and the heap allocation is leaked (benign at process exit, but an unbounded pattern if shutdown is delayed).

**Verifier reasoning:** The mechanism is real and unguarded: post_log (main.cpp:152) and the idle_set_toggle re-arm (main.cpp:614) allocate new std::string / new ToggleSet and hand them to g_idle_add, freed only inside the idle handlers (delete s at line 145, delete t in idle_set_toggle). After gtk_main_quit stops the loop, a payload queued by a still-running detached thread (command threads 467/481/610, the SDK status-callback thread via post_log at 291, or the self-test tail) is never dispatched, so its delete never runs -- a genuine leak, not a misread. But it does not bite at runtime: gtk_main_quit only ends the current gtk_main() invocation, and main() (lines 1047-1052) returns immediately afterward with no second loop or cleanup phase, so the process exits and the OS reclaims all heap. The window is bounded by the few in-flight threads at exit, so the 'unbounded if shutdown is delayed' escalation does not materialize -- shutdown is not delayed. No crash, use-after-free, or resource exhaustion during the app's lifetime. Real but benign and minor.</reason>
</invoke>


### 10. ⚪ [LOW] `gui/main.cpp:539` — sdk-correctness
*Verdict: **CONFIRMED** · finder severity: low → corrected: low*

**What:** The "1x" zoom reset is unnecessarily gated on cameraGetZoomAbsoluteR succeeding: if the getter momentarily fails, zoom_apply returns early and never issues the absolute set, so reset silently no-ops.

**Failure scenario:** on_zoom_reset calls zoom_apply(absolute=true, amount=1.0) (line 553). Inside zoom_apply the worker first calls cameraGetZoomAbsoluteR (line 538) purely to log the "from" value, and on rc != RM_RET_OK it does `return;` (line 539-542) before ever calling cameraSetZoomAbsoluteR. So if the read transiently fails, pressing "1x" logs "read zoom failed" and performs no reset at all — even though the reset target (1.0) is a constant that does not require the current value. The set should proceed regardless for the absolute-reset path.

**Verifier reasoning:** Verified against gui/main.cpp:531-553. on_zoom_reset calls zoom_apply(amount=1.0, absolute=true). The worker calls cameraGetZoomAbsoluteR(z) at line 538 and does `return;` at 539-542 on rc != RM_RET_OK, before cameraSetZoomAbsoluteR at 544. Line 543 computes nz = absolute ? amount : z+amount, so for the reset path nz=1.0f is a constant independent of z — the getter is used only to log the "from" value fmt2(z) at 545. Thus a transient getter failure makes the 1x reset silently no-op, defeating the button whose whole job is to force zoom to 1x regardless of the current reading. (Note: for the relative zoom in/out paths the early return is legitimately required since nz depends on z; the defect is specific to the absolute-reset path, which the candidate states correctly.) The trace is concrete; only the frequency of getter failure is uncertain, keeping this genuinely low severity.


### 11. ⚪ [LOW] `gui/main.cpp:1010` — logic-ui
*Verdict: **PLAUSIBLE** · finder severity: medium → corrected: low*

**What:** --self-test exit code violates its own documented contract on a headless/no-display host, returning 0 ("device found") when GTK cannot init.

**Failure scenario:** The self-test contract (lines 328-330, 1049-1051) is: exit 0 = device found, 3 = no device. On a headless CI runner gtk_init_check() fails, so line 1010 returns `g_self_test ? 0 : 1` = 0. A CI gate keying on exit code 0 concludes a camera is present and the device check passed, when in fact discovery never even ran. It should return the no-device code (3) or a distinct 'no display' code, not 0.

**Verifier reasoning:** The code at gui/main.cpp:1010 does exactly what the finding states: in self-test mode, if gtk_init_check() fails on a no-display host, it returns 0, while the self-test's own documented exit-code contract (lines 328-330 and 1049-1051) reserves 0 for "device found" and 3 for "no device." So exit code 0 genuinely carries two conflicting meanings across the two exit paths, and a CI gate that keys on 0 as "camera present" could be misled on a headless runner where discovery never ran. However, the surrounding comment (lines 1008-1010) shows this is a deliberate choice: 0 is used here in the conventional "build OK / success" sense (distinct from the code 1 returned in the non-self-test no-display case), a clear stderr diagnostic is printed, and the "0 = device found" semantics arguably only apply to the path that actually reaches gtk_main() and runs discovery. There is no in-repo caller demonstrating the misinterpretation; the failure depends entirely on an assumed external CI consumer. Real ambiguity, but uncertain impact and plausibly intended behavior — hence PLAUSIBLE at reduced severity rather than CONFIRMED.


### 12. ⚪ [LOW] `gui/main.cpp:1001` — logic-ui
*Verdict: **CONFIRMED** · finder severity: low → corrected: low*

**What:** --help text states the wrong --wait-ms default (3000) while the actual default is 6000. | --help text states discovery default is 3000 ms, but the actual default is 6000 ms

**Failure scenario:** g_wait_ms is initialized to 6000 (line 110), but the usage text printed at line 1001 says 'default 3000'. A user reading --help mis-sizes the discovery timeout expectation (self-test safety timeout is wait_ms+3000, so they'd expect ~6s but actually get ~9s).

**Verifier reasoning:** Verified in code: gui/main.cpp:110 initializes `int g_wait_ms = 6000;` and nothing changes it before the help path. The `--help`/`-h` handler at gui/main.cpp:999-1004 prints "--wait-ms N   USB discovery settle time (default 3000)" at line 1001, which contradicts the real 6000 ms default. A user reading --help would mis-size their expectation of the discovery/self-test timeout. It is a genuine, concrete doc-vs-code mismatch, but purely cosmetic (a printed string) — no crash, no wrong runtime behavior, no data corruption. Correctly rated low.


### 13. ⚪ [LOW] `gui/main.cpp:919` — logic-ui
*Verdict: **CONFIRMED** · finder severity: low → corrected: low*

**What:** FOV combo is never given an initial active index, so it starts blank (-1) and never reflects/initializes the current FOV, inconsistent with the step/speed combos.

**Failure scenario:** step_combo (gtk_combo_box_set_active 1) and speed_combo (set_active 0) get defaults, but fov_combo (lines 919-922) is left at active index -1, showing an empty box. on_fov_changed guards `i < 0` so nothing is sent, and there is no read-back of the device's actual FOV, so the control shows no state until the user manually picks an entry.

**Verifier reasoning:** Verified against gui/main.cpp:919-926: fov_combo appends three entries ("Wide 86°", "Medium 78°", "Narrow 65°") but never calls gtk_combo_box_set_active, so it starts at active index -1 (blank). By contrast step_combo (line 869) uses set_active 1 and speed_combo (line 875) uses set_active 0. A grep of the file shows no set_active for fov_combo and no read-back path (line 267 only syncs ai_toggle from device status; nothing updates fov_combo). on_fov_changed (line 559) returns early when i < 0, so the blank state sends nothing. Concrete runtime effect: the FOV control renders empty and reflects no state until the user manually selects an entry, inconsistent with the other two combos. Real but purely cosmetic/UX in a PoC, so low severity is correct.


### 14. ⚪ [LOW] `gui/main.cpp:91` — cleanup
*Verdict: **CONFIRMED** · finder severity: low → corrected: low*

**What:** StatusSnapshot.zoom_raw is dead: written but never read

**Failure scenario:** zoom_raw is declared (line 91) and assigned from st->tiny.zoom_ratio in on_dev_status (line 278) and copied through the snapshot, but is never read anywhere. The displayed zoom comes exclusively from zoom_x/zoom_valid (lines 253-254), which are populated by refresh_zoom_snapshot via cameraGetZoomAbsoluteR. The field (and its per-push read of st->tiny.zoom_ratio) is leftover after the switch to the real zoom getter and should be removed to avoid implying the raw ratio is used for anything.

**Verifier reasoning:** Traced every reference: StatusSnapshot.zoom_raw is only written (declaration at gui/main.cpp:91, assignment `s.zoom_raw = st->tiny.zoom_ratio;` at line 278, carried into g_status by the whole-struct copy at line 289) and never read anywhere. The zoom shown in the UI (lines 253-254) comes solely from zoom_x/zoom_valid, which are populated independently by the real getter (lines 236-237) and preserved across status pushes (lines 287-288). So zoom_raw and its per-push read of st->tiny.zoom_ratio are genuinely dead. Accurate but non-behavioral — this is a low-severity cleanliness finding, not a runtime bug.

---

## Triage & recommended actions

My read on each, grouped by what I'd actually do. Nothing here is critical or a
mid-use crash — the Medium cluster is almost all **process-shutdown** UB and
**honesty/UX** gaps, plus a few trivial doc/dead-code fixes.

### Worth fixing (clear, low-risk)
- **#4 — gimbal move logs "(ok)" while AI is ON (honesty).** When AI Track is on,
  the SDK keeps the gimbal and manual moves are ignored; the log still says ok.
  Fix: for PTZ/Center/preset-Go, either disable AI first (`cameraSetAiModeU(None)`)
  or, if AI is on, log a clear "ignored: AI tracking owns the gimbal" instead of ok.
- **#12 — `--help` says `--wait-ms` default 3000; actual is 6000.** One-word fix.
- **#13 — FOV combo starts blank (active index −1).** Set an initial index (or a
  "—" placeholder) like step/speed.
- **#10 — "1x" zoom reset early-returns if the getter momentarily fails.** For an
  absolute reset, skip the getter and just set 1.0.
- **#14 — `StatusSnapshot.zoom_raw` is dead (written, never read).** Remove it.
- **#5/#7 — AI toggle can diverge from real state.** Add revert-on-failure (like
  face focus) and re-sync the toggle for undecoded AI modes.

### Worth fixing (slightly larger)
- **#1 / #3 / #2 / #9 — shutdown lifecycle (one root cause).** Detached command/
  status threads are never joined and `enableDevStatusCallback(true)` is never
  disabled, so closing the window mid-command can lock destroyed mutexes /
  touch a freed toggle during teardown. Shutdown-only (crash/UB at exit), not a
  mid-use bug. Fix: on window destroy, disable the status callback and either
  join outstanding workers or guard shutdown (e.g. an atomic "quitting" flag
  checked before `g_idle_add`, and skip static-lifetime teardown races). This is
  the highest-value structural fix.
- **#6 — no hot-unplug handling.** After USB unplug, `g_dev` isn't cleared and
  controls stay enabled → commands go to a stale handle (SDK returns errors,
  logged). Already listed as a known limitation; a `setDevChangedCallback`
  unplug path would resolve it.

### Low priority / judgment calls
- **#8 — status-push resync can fight an in-flight user toggle** (brief visual
  flip until the device confirms). Minor; a short "pending" suppression window
  would smooth it.
- **#11 — `--self-test` returns 0 on a headless host with no display.** Edge
  case; only affects CI without a display. Could return a distinct code.

### Not changing
- The AI-on-Tiny3 path (`cameraSetAiModeU`) stays as-is: rc is logged honestly,
  so an unsupported call is visible rather than faked (by design).
