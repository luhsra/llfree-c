# Compiler and flags
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -Werror -fPIE
DEBUG ?= 1

# The rust wrapper calls this with DEBUG=1 on debug and DEBUG=0 on release builds
ifeq ($(DEBUG), 1)
	CFLAGS += -g -O1
else
	CFLAGS += -O3
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

.PHONY: all clean
