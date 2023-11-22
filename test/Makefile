CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -Werror -Wno-unused-parameter -pthread
LDFLAGS ?= -pthread

ifeq ($(V),)
Q := @
else
Q :=
endif

all: test test_host_ddr mmap_access
.PHONY: all

%.o: %.c Makefile
	@printf "  CC   $@\n"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

test: test.o
	@printf "  LNK  $@\n"
	$(Q)$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $^

test_host_ddr: test_host_ddr.o
	@printf "  LNK  $@\n"
	$(Q)$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $^

mmap_access: mmap_access.o
	@printf "  LNK  $@\n"
	$(Q)$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $^

clean:
	@rm -f *.o
	@rm -f test
	@rm -f test_host_ddr
	@rm -f mmap_access
.PHONY: all
