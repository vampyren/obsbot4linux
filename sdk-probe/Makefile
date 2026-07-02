# Makefile for obsbot-sdk-probe (Linux / x86_64)
#
# The repo has no pre-existing build system (the SDK ships only a Windows/macOS
# oriented CMakeLists), and cmake is not required to build this spike. A plain
# Makefile keeps the dependency surface minimal: just g++ and the SDK tree.
#
# Runtime library loading:
#   The SDK's SONAME is "libdev.so.1.0.3". We embed an $ORIGIN-relative rpath so
#   the built binary locates the SDK .so next to the source tree WITHOUT any
#   global install (no ldconfig, no /usr/local copy). $ORIGIN resolves to the
#   directory of the executable at load time.

SDK_ROOT   ?= ../sdk/libdev_v2.1.0_8
SDK_INC     = $(SDK_ROOT)/include
SDK_LIB     = $(SDK_ROOT)/linux/x86_64-release

TARGET      = obsbot-sdk-probe
SRC         = main.cpp

CXX        ?= g++
CXXFLAGS   ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS   += -I$(SDK_INC)

# Link against libdev.so in the Linux x86_64 dir. The $ORIGIN rpath is relative
# to the built binary's location (this directory), so ../libdev.../linux/... .
LDFLAGS    += -L$(SDK_LIB) '-Wl,-rpath,$$ORIGIN/$(SDK_LIB)'
LDLIBS     += -ldev -lpthread

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

# Convenience: run with an explicit LD_LIBRARY_PATH fallback in case the binary
# is moved away from this directory (which would break the $ORIGIN rpath).
run: $(TARGET)
	LD_LIBRARY_PATH=$(SDK_LIB):$$LD_LIBRARY_PATH ./$(TARGET)

clean:
	rm -f $(TARGET)
