# Standalone integration-test harness: MiSTer streamed-frame fidelity.
# NOT wired into CI. Build + run:   make run
# From repo root:                   make mister-integration-test
#

ROOT_DIR := ../../..
CC       ?= gcc
CXX      ?= g++

CRIT_CFLAGS := $(shell pkg-config --cflags criterion)
CRIT_LIBS   := $(shell pkg-config --libs criterion)

LZ4_DIR  := $(ROOT_DIR)/deps/lz4

WARN     := -Wall -Wextra -Wno-unused-parameter
CFLAGS   ?= -O0 -g $(WARN)
INCFLAGS := -I. -I$(LZ4_DIR) $(CRIT_CFLAGS)

# --- real RetroArch sources compiled into the test (the unit under test) ---
RA          := $(ROOT_DIR)
RA_INC      := -I$(RA) -I$(RA)/libretro-common/include -I$(RA)/deps \
               -I$(RA)/deps/switchres -I$(RA)/deps/mister -I$(RA)/gfx
RA_DEF      := -DHAVE_MISTER -DHAVE_MENU
SCALER      := $(RA)/libretro-common/gfx/scaler

# --- Test sources ---
TEST_SRCS := spec_smoke.c \
             unit/test_ssim.c \
             unit/test_convert.c \
             unit/test_factories.c \
             unit/test_composer.c \
             spec_transport.c \
             spec_link.c \
             spec_frame_fidelity.c

# --- Production / simulator / seam objects under test ---
SUPPORT_SRCS := metrics/ssim.c \
                metrics/compare.c \
                metrics/vmaf.c \
                sim/factories.c \
                sim/state_machine.c \
                sim/udp_transport.c \
                seams/fake_config.c \
                seams/fake_audio.c \
                seams/fake_menu.c \
                seams/fake_video.c \
                seams/fake_verbosity.c \
                driver/mister_stream_driver.c

OBJDIR    := obj
ALL_SRCS  := $(TEST_SRCS) $(SUPPORT_SRCS)
ALL_OBJS  := $(patsubst %.c,$(OBJDIR)/%.o,$(ALL_SRCS))

# Vendored objects compiled from outside this tree (kept under obj/vendor/).
VENDOR_OBJS := $(OBJDIR)/vendor/lz4.o $(OBJDIR)/vendor/lz4hc.o

# Real RetroArch objects under test (the production code we exercise).
REAL_OBJS := $(OBJDIR)/ra/gfx_mister.o \
             $(OBJDIR)/ra/scaler.o \
             $(OBJDIR)/ra/pixconv.o \
             $(OBJDIR)/ra/scaler_int.o \
             $(OBJDIR)/ra/scaler_filter.o \
             $(OBJDIR)/ra/groovymister.o \
             $(OBJDIR)/ra/groovymister_wrapper.o

BIN       := mister_tests

.PHONY: all run clean
all: $(BIN)

# Generic test/support sources (our own code).
$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCFLAGS) -c -o $@ $<

# Seams include real RetroArch headers → need the RA defines/includes.
$(OBJDIR)/seams/%.o: seams/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_DEF) $(RA_INC) -c -o $@ $<

# Driver bridges our sim headers (-I.) and real RetroArch headers.
$(OBJDIR)/driver/%.o: driver/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I. $(RA_DEF) $(RA_INC) -c -o $@ $<

# Vendored LZ4.
$(OBJDIR)/vendor/lz4.o: $(LZ4_DIR)/lz4.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(LZ4_DIR) -c -o $@ $<
$(OBJDIR)/vendor/lz4hc.o: $(LZ4_DIR)/lz4hc.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(LZ4_DIR) -c -o $@ $<

# Real RetroArch C sources.
$(OBJDIR)/ra/gfx_mister.o: $(RA)/gfx/gfx_mister.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_DEF) $(RA_INC) -c -o $@ $<
$(OBJDIR)/ra/scaler.o: $(SCALER)/scaler.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_INC) -c -o $@ $<
$(OBJDIR)/ra/pixconv.o: $(SCALER)/pixconv.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_INC) -c -o $@ $<
$(OBJDIR)/ra/scaler_int.o: $(SCALER)/scaler_int.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_INC) -c -o $@ $<
$(OBJDIR)/ra/scaler_filter.o: $(SCALER)/scaler_filter.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RA_INC) -c -o $@ $<

# Real GroovyMiSTer C++ sender.
$(OBJDIR)/ra/groovymister.o: $(RA)/deps/mister/groovymister.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(RA)/deps -I$(RA)/deps/mister -c -o $@ $<
$(OBJDIR)/ra/groovymister_wrapper.o: $(RA)/deps/mister/groovymister_wrapper.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) -I$(RA)/deps -I$(RA)/deps/mister -c -o $@ $<

$(BIN): $(ALL_OBJS) $(VENDOR_OBJS) $(REAL_OBJS)
	$(CC) -o $@ $(ALL_OBJS) $(VENDOR_OBJS) $(REAL_OBJS) $(CRIT_LIBS) -lm -pthread -lstdc++

# -j1: tests share the hardcoded MiSTer UDP port 32100, so they must run
# serially (parallel workers would cross-talk on the socket).
run: $(BIN)
	./$(BIN) -j1

clean:
	rm -rf $(OBJDIR) $(BIN)
