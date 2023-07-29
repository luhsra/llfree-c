# Compiler and flags
CC = clang
AR = ar
CFLAGS = -Wall -Wextra -Werror -fPIE -std=gnu11 -pthread -I $(PWD)
DEBUG ?= 1

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O3 -D NDEBUG
endif

# Library name, sources, and build directory
LIBNAME = libllc.a
SRCS = $(wildcard *.c)
OBJS = $(addprefix build/,$(SRCS:.c=.o))
BUILDDIR = build

# Default target
all: $(BUILDDIR)/$(LIBNAME)

# Rule to create the build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Rule to compile .c files into .o files
build/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to create the static library
$(BUILDDIR)/$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Clean target
clean:
	rm -rf $(BUILDDIR)

TESTSRCS = $(wildcard tests/*.c)
TESTOBJS = $(addprefix build/,$(TESTSRCS:.c=.o))
TESTDIR = tests

$(TESTDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(TESTDIR)


build/tests/%.o: tests/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@


test_build: $(TESTDIR) $(OBJS) $(TESTOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(TESTOBJS) -o $(BUILDDIR)/$(TESTDIR)/run_tests

test: test_build
	./$(BUILDDIR)/$(TESTDIR)/run_tests

debug: test_build
	gdb ./$(BUILDDIR)/$(TESTDIR)/run_tests


BENCHSRCS = $(wildcard bench/*.c)
BENCHOBJS = $(addprefix build/,$(BENCHSRCS:.c=.o))
BENCHDIR = bench

$(BENCHDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(BENCHDIR)

build/bench/%.o: bench/%.c | $(BENCHDIR)
	$(CC) $(CFLAGS) -c $< -o $@

benchmark_build: $(BENCHDIR) $(OBJS) $(BENCHOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(BENCHOBJS) -o $(BUILDDIR)/$(BENCHDIR)/benchmark

benchmark: benchmark_build
	./$(BUILDDIR)/$(BENCHDIR)/benchmark allHP_MC

.PHONY: all clean test debug
