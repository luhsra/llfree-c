# Compiler and flags
CC = clang
AR = ar
CFLAGS = -Wall -Wextra -Werror -fPIE -std=c11 -I $(PWD)
DEBUG ?= 1

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
ifeq ($(DEBUG), 1)
	CFLAGS += -g -O1
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

TESTSRCS = $(wildcard tests/*s.c)
TESTOBJS = $(addprefix build/,$(TESTSRCS:.c=.o))
TESTDIR = tests


$(TESTDIR): $(BUILDDIR)
	mkdir -p $(BUILDDIR)/$(TESTDIR)

build/tests/%.o: tests/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# LDFLAGS = -I $(TESTOBJS)
test: clean $(TESTDIR) $(TESTOBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) tests/test.c $(TESTOBJS) -o $(BUILDDIR)/$(TESTDIR)/run_tests
	./$(BUILDDIR)/$(TESTDIR)/run_tests

debug: clean $(TESTDIR) $(TESTOBJS)
	$(CC) $(CFLAGS) -g $(LDFLAGS) tests/test.c $(TESTOBJS) -o $(BUILDDIR)/$(TESTDIR)/run_tests
	gdb ./$(BUILDDIR)/$(TESTDIR)/run_tests

.PHONY: all clean test debug
