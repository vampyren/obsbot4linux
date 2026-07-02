// obsbot-sdk-probe
//
// Minimal Linux SDK integration spike for the OBSBOT Tiny 3.
//
// Purpose: prove that the official OBSBOT device SDK (libdev v2.1.0_8)
// links and runs on Linux/x86_64, can discover a USB-connected camera,
// report its identity, and issue a small set of *safe* control commands.
//
// This is intentionally NOT the full GUI. It only exercises discovery and a
// handful of low-risk commands so we can validate the SDK path before
// investing in a Wayland-native control app.
//
// See README.md in this directory for build/run instructions and the
// runtime library-loading story.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <dev/devs.hpp>

namespace {

// Default USB discovery settling time. The SDK spins up a background USB
// detection task on first Devices::get(); give it a moment to enumerate.
constexpr int kDefaultWaitMs = 3000;

struct Options {
    bool status = false;   // print live camera status snapshot
    bool wake = false;     // set device run state -> Run
    bool sleep = false;    // set device run state -> Sleep
    bool center = false;   // reset gimbal to zero position (safe, no loops)
    bool enable_mdns = false;
    int wait_ms = kDefaultWaitMs;
    std::string sn;        // optional: target a specific device by SN
};

const char *productName(ObsbotProductType type) {
    switch (type) {
    case ObsbotProdTiny:        return "Tiny";
    case ObsbotProdTiny4k:      return "Tiny4K";
    case ObsbotProdTiny2:       return "Tiny2";
    case ObsbotProdTiny2Lite:   return "Tiny2Lite";
    case ObsbotProdTailAir:     return "TailAir";
    case ObsbotProdMeet:        return "Meet";
    case ObsbotProdMeet4k:      return "Meet4K";
    case ObsbotProdMe:          return "Me";
    case ObsbotProdHDMIBox:     return "HDMIBox";
    case ObsbotProdNDIBox:      return "NDIBox";
    case ObsbotProdMeet2:       return "Meet2";
    case ObsbotProdTail2:       return "Tail2";
    case ObsbotProdTinySE:      return "TinySE";
    case ObsbotProdMeetSE:      return "MeetSE";
    case ObsbotProdTail2S:      return "Tail2S";
    case ObsbotProdTiny3:       return "Tiny3";      // enum value 18
    case ObsbotProdTiny3Lite:   return "Tiny3Lite";  // enum value 19
    default:                    return "Unknown";
    }
}

const char *devModeName(Device::DevMode mode) {
    switch (mode) {
    case Device::DevModeUvc: return "UVC";
    case Device::DevModeNet: return "Net";
    case Device::DevModeMtp: return "MTP";
    case Device::DevModeBle: return "BLE";
    default:                 return "Unknown";
    }
}

void printUsage(const char *argv0) {
    std::cout <<
        "Usage: " << argv0 << " [options]\n"
        "\n"
        "Discovers OBSBOT USB devices via the official SDK and prints their\n"
        "identity. With no command flag it only lists devices (read-only).\n"
        "\n"
        "Options:\n"
        "  --status        Print a live status snapshot (zoom / AI mode)\n"
        "  --wake          Set the selected device run state to Run\n"
        "  --sleep         Set the selected device run state to Sleep\n"
        "  --center        Reset the gimbal to its zero position (safe)\n"
        "  --sn <SN>       Target the device with this 14-char serial number\n"
        "  --wait-ms <N>   USB discovery settle time in ms (default "
                           << kDefaultWaitMs << ")\n"
        "  --enable-mdns   Enable mDNS/network scan (off by default)\n"
        "  -h, --help      Show this help\n"
        "\n"
        "Exit codes: 0 = at least one OBSBOT device found; non-zero otherwise.\n";
}

// Returns false if parsing failed / help was requested (caller should exit).
bool parseArgs(int argc, char **argv, Options &opt, int &exit_code) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            printUsage(argv[0]);
            exit_code = 0;
            return false;
        } else if (a == "--status") {
            opt.status = true;
        } else if (a == "--wake") {
            opt.wake = true;
        } else if (a == "--sleep") {
            opt.sleep = true;
        } else if (a == "--center") {
            opt.center = true;
        } else if (a == "--enable-mdns") {
            opt.enable_mdns = true;
        } else if (a == "--sn") {
            if (i + 1 >= argc) {
                std::cerr << "error: --sn requires a value\n";
                exit_code = 2;
                return false;
            }
            opt.sn = argv[++i];
        } else if (a == "--wait-ms") {
            if (i + 1 >= argc) {
                std::cerr << "error: --wait-ms requires a value\n";
                exit_code = 2;
                return false;
            }
            opt.wait_ms = std::atoi(argv[++i]);
            if (opt.wait_ms < 0) opt.wait_ms = 0;
        } else {
            std::cerr << "error: unknown argument '" << a << "'\n";
            printUsage(argv[0]);
            exit_code = 2;
            return false;
        }
    }
    if (opt.wake && opt.sleep) {
        std::cerr << "error: --wake and --sleep are mutually exclusive\n";
        exit_code = 2;
        return false;
    }
    return true;
}

void printDevice(const std::shared_ptr<Device> &dev, int index) {
    const ObsbotProductType type = dev->productType();
    const bool is_tiny3 = (type == ObsbotProdTiny3);

    std::cout << "-----------------------------------------------------\n";
    std::cout << "  index:        " << index << "\n";
    std::cout << "  name:         " << dev->devName() << "\n";
    std::cout << "  SN:           " << dev->devSn() << "\n";
    std::cout << "  version:      " << dev->devVersion() << "\n";
    std::cout << "  product:      " << productName(type)
              << " (enum " << static_cast<int>(type) << ")\n";
    std::cout << "  mode:         " << devModeName(dev->devMode()) << "\n";
    std::cout << "  is Tiny 3:    " << (is_tiny3 ? "YES" : "no") << "\n";
}

// Live status snapshot for the tiny family. Read-only.
void printStatus(const std::shared_ptr<Device> &dev) {
    const Device::CameraStatus status = dev->cameraStatus();
    const ObsbotProductType type = dev->productType();
    std::cout << "  status snapshot:\n";
    switch (type) {
    case ObsbotProdTiny:
    case ObsbotProdTiny4k:
    case ObsbotProdTiny2:
    case ObsbotProdTiny3:
    case ObsbotProdTiny3Lite:
        std::cout << "    zoom_ratio: " << status.tiny.zoom_ratio << "\n";
        std::cout << "    ai_mode:    " << status.tiny.ai_mode << "\n";
        break;
    default:
        std::cout << "    (no tiny-family status view for this product)\n";
        break;
    }
}

} // namespace

int main(int argc, char **argv) {
    Options opt;
    int exit_code = 0;
    if (!parseArgs(argc, argv, opt, exit_code)) {
        return exit_code;
    }

    std::cout << "obsbot-sdk-probe: SDK lib version " << get_dll_ver() << "\n";

    // Devices::get() is a global singleton; first access starts the SDK's
    // background USB detection task.
    Devices &devices = Devices::get();

    // Keep the spike focused on USB. mDNS/network scan is off unless asked.
    devices.setEnableMdnsScan(opt.enable_mdns);

    std::cout << "Waiting " << opt.wait_ms << " ms for USB discovery"
              << (opt.enable_mdns ? " (mDNS enabled)" : "") << " ...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(opt.wait_ms));

    std::list<std::shared_ptr<Device>> dev_list = devices.getDevList();

    if (dev_list.empty()) {
        std::cerr << "\nERROR: no OBSBOT device found.\n"
                     "Diagnostics:\n"
                     "  - Is the Tiny 3 connected via USB and powered?\n"
                     "  - Try a longer settle time: --wait-ms 6000\n"
                     "  - Check `lsusb` for an OBSBOT / Remo entry.\n"
                     "  - USB control may need udev permissions; try running\n"
                     "    with sufficient privileges or add a udev rule.\n";
        return 1;
    }

    std::cout << "\nFound " << dev_list.size()
              << " OBSBOT device(s):\n";

    int index = 0;
    bool any_tiny3 = false;
    for (const auto &dev : dev_list) {
        printDevice(dev, index++);
        if (dev->productType() == ObsbotProdTiny3) {
            any_tiny3 = true;
        }
    }

    // Select the device to act on: --sn if given, else the first one.
    std::shared_ptr<Device> target;
    if (!opt.sn.empty()) {
        target = devices.getDevBySn(opt.sn);
        if (!target) {
            std::cerr << "\nERROR: no device with SN '" << opt.sn
                      << "' is connected.\n";
            return 1;
        }
    } else {
        target = dev_list.front();
    }

    const bool want_command = opt.status || opt.wake || opt.sleep || opt.center;
    if (want_command) {
        std::cout << "\nActing on device: " << target->devName()
                  << " (SN " << target->devSn() << ")\n";
    }

    if (opt.status) {
        printStatus(target);
    }

    // Safe command path. No movement loops, no aggressive motion.
    if (opt.wake) {
        int32_t rc = target->cameraSetDevRunStatusR(Device::DevStatusRun);
        std::cout << "  --wake  cameraSetDevRunStatusR(Run)  -> rc=" << rc << "\n";
    }
    if (opt.sleep) {
        int32_t rc = target->cameraSetDevRunStatusR(Device::DevStatusSleep);
        std::cout << "  --sleep cameraSetDevRunStatusR(Sleep) -> rc=" << rc << "\n";
    }
    if (opt.center) {
        // gimbalRstPosR() moves the gimbal back to the zero/home position.
        // This is a single bounded reset, not a continuous motion command.
        int32_t rc = target->gimbalRstPosR();
        std::cout << "  --center gimbalRstPosR()             -> rc=" << rc << "\n";
    }

    std::cout << "\nTiny 3 detected: " << (any_tiny3 ? "YES" : "no") << "\n";

    // Release SDK resources / stop the detection task cleanly.
    devices.close();
    return 0;
}
