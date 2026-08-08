# Builds the real signature and scoring engine for the host and takes it apart.
#
# Everything Skimscan puts on screen is a rendering of what skim_score()
# decided, and a screenshot cannot vouch for any of it -- so the tables, the
# boundaries, the caps and the invariants are checked on every push.
#
#   make -C test

CC      ?= cc
CFLAGS  := -std=gnu2x -Wall -Wextra -Werror -O1 -g
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  += -I ..

ENGINE := ../helpers/skim_sigs.c ../helpers/skim_score.c
HDR    := ../helpers/skim_sigs.h ../helpers/skim_score.h

SRC := host_score_test.c $(ENGINE)
BIN := host_score_test

DUMP_SRC := host_demo_dump.c ../helpers/skim_demo.c $(ENGINE)
DUMP_BIN := host_demo_dump

.PHONY: all run dump clean
all: run

$(BIN): $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $@

run: $(BIN)
	./$(BIN)

# Scores the scripted forecourt with the real engine and writes the JSON the
# mockup renderer draws from -- so the README screenshots cannot drift away
# from what the app actually does.
$(DUMP_BIN): $(DUMP_SRC) $(HDR) ../helpers/skim_demo.h
	$(CC) $(CFLAGS) $(DUMP_SRC) -o $@

dump: $(DUMP_BIN)
	./$(DUMP_BIN) > demo_dump.json
	@echo "wrote test/demo_dump.json"

clean:
	rm -rf $(BIN) $(DUMP_BIN) $(BIN).dSYM $(DUMP_BIN).dSYM demo_dump.json
