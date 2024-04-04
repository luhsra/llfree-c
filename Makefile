BUILDDIR ?= build
A ?= ""

SRCDIR = src
TESTDIR = tests

# Compiler and flags
CC = clang
AR = ar
CFLAGS = -std=c11 -Werror -Wunused-variable -Wundef -Werror=strict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wvla -Wcast-function-type -Wimplicit-fallthrough -Werror=date-time -Werror=incompatible-pointer-types -Wconversion -Wenum-conversion -Wint-conversion -Wimplicit-int-conversion -Wimplicit-float-conversion -Wundefined-bool-conversion -Wbitfield-enum-conversion -Wanon-enum-enum-conversion -Wmissing-field-initializers -Wall -Wextra -fPIE -pthread

CFLAGS += -I $(SRCDIR) -I $(TESTDIR) -I include -I std
CFLAGS += -DSTD

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CFLAGS += -g -DVERBOSE
else
	CFLAGS += -O3 -g -march=native -fomit-frame-pointer
endif

# Library name, sources, and build directory
LIB = $(BUILDDIR)/libllc.a
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

all: $(LIB)

$(BUILDDIR)/$(SRCDIR):
	mkdir -p $(BUILDDIR)/$(SRCDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)/$(SRCDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Create the static library
$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# --- Tests ---

TESTSRCS = $(wildcard $(TESTDIR)/*.c)
TESTOBJS = $(addprefix $(BUILDDIR)/,$(TESTSRCS:.c=.o))

$(BUILDDIR)/$(TESTDIR):
	mkdir -p $(BUILDDIR)/$(TESTDIR)

test_build: $(BUILDDIR)/$(TESTDIR) $(LIB) $(TESTOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TESTOBJS) $(LIB) -o $(BUILDDIR)/$(TESTDIR)/tests

test: test_build
	./$(BUILDDIR)/$(TESTDIR)/tests $(A)


clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean test

ALL_OBJS = $(OBJS) $(TESTOBJS) $(BENCHOBJS)
ALL_DEPS = $(ALL_OBJS:.o=.d)

-include $(ALL_DEPS)
