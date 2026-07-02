# Makefile for the OBSBOT Tiny 3 Control GUI PoC (Linux / x86_64, GTK3)
#
# Repo-local runtime loading: the SDK SONAME is libdev.so.1.0.3. We embed an
# $ORIGIN-relative rpath so the built binary finds the SDK .so from the repo
# tree with NO global install (no ldconfig, no copy into /usr/lib).
#
# $ORIGIN resolves to the directory of the executable (this gui/ dir), so
# ../sdk/libdev_v2.1.0_8/linux/x86_64-release is the repo-root SDK lib dir.

SDK_ROOT   ?= ../sdk/libdev_v2.1.0_8
SDK_INC     = $(SDK_ROOT)/include
SDK_LIB     = $(SDK_ROOT)/linux/x86_64-release

TARGET      = obsbot-tiny3-control-bin
SRC         = main.cpp

CXX        ?= g++
CXXFLAGS   ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS   += -I$(SDK_INC) $(shell pkg-config --cflags gtk+-3.0)

LDFLAGS    += -L$(SDK_LIB) '-Wl,-rpath,$$ORIGIN/$(SDK_LIB)'
LDLIBS     += -ldev -lpthread $(shell pkg-config --libs gtk+-3.0)

.PHONY: all clean run self-test test

all: $(TARGET)

# Hardware-free regression test for the discovery poll timing (poll.hpp).
test: poll_logic_test
	./poll_logic_test

poll_logic_test: poll_logic_test.cpp poll.hpp
	$(CXX) $(CXXFLAGS) poll_logic_test.cpp -o poll_logic_test -lpthread

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# Smoke test: builds UI, polls for a device up to the timeout, then exits.
# Exit code reflects the DEVICE result (0 = found, 3 = none by timeout), so this
# target uses a leading '-' to not abort `make` on a box with no camera attached.
# The "[self-test] UI: OK" line still confirms the GUI built and ran.
self-test: $(TARGET)
	-./$(TARGET) --self-test

clean:
	rm -f $(TARGET) poll_logic_test
