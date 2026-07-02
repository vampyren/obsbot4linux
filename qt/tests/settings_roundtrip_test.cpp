// Hardware-free persistence test for Settings (Rex finding #7: presets must
// survive restart). Writes a config, reads it back, asserts equality. Build/run
// with QtCore only — no SDK, no camera, no GUI. Run via `ctest --test-dir qt/build`.
#include "../src/Settings.h"

#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__); ++failures; } } while (0)

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // Route the config to a throwaway path via the documented env override.
    const QByteArray tmp = "/tmp/obsbot_settings_test.json";
    qputenv("OBSBOT_TINY3_CONFIG", tmp);
    std::remove(tmp.constData());

    // Defaults when the file is absent.
    AppSettings def = Settings::load();
    CHECK(def.moveStepDeg == 5);
    CHECK(def.fovIndex == 0);
    CHECK(def.presets[0].set == false);

    // Mutate + persist (mirrors what the controller does on user actions).
    AppSettings s;
    s.moveStepDeg = 10;
    s.speedMode = 1;
    s.fovIndex = 2;
    s.startupPreset = 2;
    s.aiReturnPreset = 3;
    s.previewResIndex = 3;
    s.presets[1].set = true;
    s.presets[1].name = "Speaker";
    s.presets[1].pan = -33.5;
    s.presets[1].tilt = 12.0;
    s.presets[1].zoom = 1.30;
    s.presets[1].fov = 1;
    CHECK(Settings::save(s));

    // Reload (simulates an app restart) and compare.
    AppSettings r = Settings::load();
    CHECK(r.moveStepDeg == 10);
    CHECK(r.speedMode == 1);
    CHECK(r.fovIndex == 2);
    CHECK(r.startupPreset == 2);
    CHECK(r.aiReturnPreset == 3);
    CHECK(r.previewResIndex == 3);
    CHECK(r.presets[1].set == true);
    CHECK(r.presets[1].name == QString("Speaker"));
    CHECK(r.presets[1].pan == -33.5);
    CHECK(r.presets[1].tilt == 12.0);
    CHECK(r.presets[1].zoom == 1.30);
    CHECK(r.presets[1].fov == 1);
    CHECK(r.presets[0].set == false);
    CHECK(r.presets[2].set == false);

    std::remove(tmp.constData());
    if (failures == 0) { std::printf("ALL SETTINGS PERSISTENCE TESTS PASS\n"); return 0; }
    std::fprintf(stderr, "%d settings test(s) FAILED\n", failures);
    return 1;
}
