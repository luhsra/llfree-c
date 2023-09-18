# Compiler and flags
CC = clang-14
AR = ar
CFLAGS = -Wall -Wextra -Werror -fPIE -std=gnu11 -pthread -I $(PWD)
DEBUG ?= 1

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O3 -D NDEBUG -march=native -fomit-frame-pointer
endif

# Library name, sources, and build directory
BUILDDIR ?= build
LIBNAME = libllc.a
SRCS = $(wildcard *.c)
OBJS = $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

# Default target
all: $(BUILDDIR)/$(LIBNAME)

# Rule to create the build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Rule to compile .c files into .o files
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Rule to create the static library
$(BUILDDIR)/$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Clean target
clean:
	rm -rf $(BUILDDIR)

TESTSRCS = $(wildcard tests/*.c)
TESTOBJS = $(addprefix $(BUILDDIR)/,$(TESTSRCS:.c=.o))
TESTDIR = tests

$(TESTDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(TESTDIR)


$(BUILDDIR)/tests/%.o: tests/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@


test_build: $(TESTDIR) $(OBJS) $(TESTOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(TESTOBJS) -o $(BUILDDIR)/$(TESTDIR)/run_tests

test: test_build
	./$(BUILDDIR)/$(TESTDIR)/run_tests

debug: test_build
	gdb ./$(BUILDDIR)/$(TESTDIR)/run_tests


BENCHSRCS = $(wildcard bench/*.c)
BENCHOBJS = $(addprefix $(BUILDDIR)/,$(BENCHSRCS:.c=.o))
BENCHDIR = bench

$(BENCHDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(BENCHDIR)

$(BUILDDIR)/bench/%.o: bench/%.c | $(BENCHDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

benchmark_build: $(BENCHDIR) $(OBJS) $(BENCHOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(BENCHOBJS) -o $(BUILDDIR)/$(BENCHDIR)/benchmark

benchmark: benchmark_build
	./$(BUILDDIR)/$(BENCHDIR)/benchmark rand

flamegraph:
	perf record -F 99 -a -g build/bench/benchmark allHP_MC
	perf script | ../../FlameGraph/stackcollapse-perf.pl > out.perf-folded
	../../FlameGraph/flamegraph.pl out.perf-folded > perf.svg
.PHONY: all clean test debug

ALL_OBJS = $(OBJS) $(TESTOBJS) $(BENCHOBJS)
ALL_DEPS = $(ALL_OBJS:.o=.d)

-include $(ALL_DEPS)
