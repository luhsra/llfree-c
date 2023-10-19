BUILDDIR ?= build
SRCDIR = src

# Compiler and flags
CC = clang
AR = ar
CFLAGS = -std=c11 -Werror -Wunused-variable -Wundef -Werror=strict-prototypes -Wno-trigraphs -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -Werror=unknown-warning-option -Werror=ignored-optimization-argument -Wno-sign-compare -Wno-frame-address -Wno-address-of-packed-member -Wno-gnu -Wno-unused-but-set-variable -Wno-unused-const-variable -Wvla -Wno-pointer-sign -Wcast-function-type -Wimplicit-fallthrough -Werror=date-time -Werror=incompatible-pointer-types -Wno-initializer-overrides -Wno-sign-compare -Wno-pointer-to-enum-cast -Wno-tautological-constant-out-of-range-compare -Wno-unaligned-access -Wno-cast-function-type-strict -fPIE -pthread -I $(PWD) -I $(PWD)/$(SRCDIR)

A ?= ""

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O3 -D NDEBUG -march=native -fomit-frame-pointer
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

TESTDIR = tests
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
