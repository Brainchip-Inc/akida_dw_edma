CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -Wno-unused-parameter

ifeq ($(V),)
Q := @
else
Q :=
endif

all: test
.PHONY: all

%.o: %.c Makefile
	@printf "  CC   $@\n"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

test: test.o
	@printf "  LNK  $@\n"
	$(Q)$(CC) $(LDFLAGS) $(LDLIBS) -o $@ $^

clean:
	@rm -f *.o
	@rm -f test
.PHONY: all
