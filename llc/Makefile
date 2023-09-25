# Compiler and flags
CC = clang
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
LIB = $(BUILDDIR)/libllc.a
SRCS = $(wildcard *.c)
OBJS = $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

# Default target
all: $(LIB)

# Rule to create the build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Rule to compile .c files into .o files
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Rule to create the static library
$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Clean target
clean:
	rm -rf $(BUILDDIR)


# --- Tests ---

TESTDIR = tests
TESTSRCS = $(wildcard $(TESTDIR)/*.c)
TESTOBJS = $(addprefix $(BUILDDIR)/,$(TESTSRCS:.c=.o))

$(BUILDDIR)/$(TESTDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(TESTDIR)

test_build: $(BUILDDIR)/$(TESTDIR) $(LIB) $(TESTOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TESTOBJS) $(LIB) -o $(BUILDDIR)/$(TESTDIR)/tests

test: test_build
	./$(BUILDDIR)/$(TESTDIR)/tests


# --- Bench ---

BENCHDIR = bench
BENCHSRCS = $(wildcard $(BENCHDIR)/*.c)
BENCHOBJS = $(addprefix $(BUILDDIR)/,$(BENCHSRCS:.c=.o))
T ?= rand

$(BUILDDIR)/$(BENCHDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(BENCHDIR)

bench_build: $(BUILDDIR)/$(BENCHDIR) $(LIB) $(BENCHOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(BENCHOBJS) $(LIB) -o $(BUILDDIR)/$(BENCHDIR)/bench

bench: bench_build
	./$(BUILDDIR)/$(BENCHDIR)/bench $(T)


.PHONY: all clean test debug

ALL_OBJS = $(OBJS) $(TESTOBJS) $(BENCHOBJS)
ALL_DEPS = $(ALL_OBJS:.o=.d)

-include $(ALL_DEPS)
