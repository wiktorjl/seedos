# Userspace test programs Makefile
#
# Builds minimal statically-linked ELF64 executables for testing SeedOS userspace.

CC = gcc
AS = as
LD = ld

# Freestanding flags - no standard library
CFLAGS = -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -static -m64 -O2 -Wall -Wextra \
         -Iinclude -ffreestanding -mno-red-zone -mno-sse

ASFLAGS = --64

# Static linking, no standard libs
LDFLAGS = -nostdlib -static -z max-page-size=0x1000

# C runtime
CRT = build/crt0.o

# Find all test source files
TESTS = $(wildcard tests/*.c)
BINS  = $(TESTS:tests/%.c=build/%)

.PHONY: all clean

all: build $(BINS)
	@echo "Built $(words $(BINS)) test programs"

build:
	mkdir -p build

# Build C runtime
build/crt0.o: crt/crt0.S | build
	$(AS) $(ASFLAGS) -o $@ $<

# Build each test program
# Pattern: tests/foo.c -> build/foo
build/%: tests/%.c $(CRT) | build
	$(CC) $(CFLAGS) -c -o build/$*.o $<
	$(LD) $(LDFLAGS) -o $@ $(CRT) build/$*.o

clean:
	rm -rf build

# Show what would be built
list:
	@echo "Tests: $(TESTS)"
	@echo "Bins: $(BINS)"
