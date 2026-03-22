CC ?= gcc
TARGET_TRIPLE ?=
CC_MACHINE := $(strip $(if $(TARGET_TRIPLE),$(TARGET_TRIPLE),$(shell $(firstword $(CC)) -dumpmachine 2>/dev/null)))
VERSION_FILE ?= VERSION
VERSION := $(strip $(shell cat $(VERSION_FILE) 2>/dev/null || echo 0.0.0-dev))

ifeq ($(OS),Windows_NT)
  IS_WINDOWS = 1
else
  IS_WINDOWS = 0
endif
ifneq (,$(findstring mingw,$(CC_MACHINE)))
  IS_WINDOWS = 1
endif
ifneq (,$(findstring windows,$(CC_MACHINE)))
  IS_WINDOWS = 1
endif
ifneq (,$(findstring msys,$(CC_MACHINE)))
  IS_WINDOWS = 1
endif

CFLAGS = -I. -Iinclude -Ivendor/miniaudio -O2 -std=c99 -DKS2_VERSION=\"$(VERSION)\"
SRC = src/main.c src/ksynth.c src/dsp.c src/audio.c src/miniaudio.c src/udp.c

ifeq ($(IS_WINDOWS),0)
  CFLAGS += -Ivendor/bestline
  SRC += vendor/bestline/bestline.c
endif

OBJ = $(SRC:.c=.o)
OUT = ksynth

.PHONY: all clean smoke version check-version aliases aliases-persist

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f $(OBJ) $(OUT)

version:
	@echo $(VERSION)

check-version:
	@grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$$' $(VERSION_FILE) || \
	( echo "Invalid semantic version in $(VERSION_FILE): $$(cat $(VERSION_FILE))" && exit 1 )

smoke: $(OUT)
	@log=$${TMPDIR:-/tmp}/ks2-smoke.log; \
	err=$${TMPDIR:-/tmp}/ks2-smoke.err; \
	printf ':load ks/dw8k-01.ks\n:wt 0 W\n:load ks/dw8k-07.ks\n:wt 1 W\n:usewt 0 1\n:chmode 0 mono\n:glide 0 120\n:noteon 0 60 100\n:sleep 20ms\n:noteoff 0 60\n:quit\n' | ./$(OUT) > $$log 2> $$err; \
	grep -q 'wavetable\[0\] loaded' $$log; \
	grep -q 'wavetable\[1\] loaded' $$log; \
	grep -q 'osc A->wt\[0\], osc B->wt\[1\]' $$log; \
	grep -q 'channel 0 mode=mono' $$log; \
	grep -q 'noteon ch=0 note=60 vel=100' $$log; \
	grep -q 'noteoff ch=0 note=60' $$log; \
	echo "smoke: PASS ($$log)"

aliases:
	@bash tools/install-aliases.sh

aliases-persist:
	@bash tools/install-aliases.sh --write-rc
