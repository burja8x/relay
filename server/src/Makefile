PLATFORM := $(shell uname -s)
ifeq ($(PLATFORM), Darwin)
	PFLAGS = -DMACOS -D_DARWIN_C_SOURCE
endif
ifeq ($(PLATFORM), Linux)
	PFLAGS = -DLINUX -D_GNU_SOURCE
endif
PROG ?= relay

all: $(PROG)

$(PROG): main.c
	gcc -g mongoose.c -Wall $(PFLAGS) -I. -W -Wcomment relay.c mole.c log.c tty.c pipe.c files.c ws.c pipe_data.c -o $(PROG) main.c

clean:
	rm -rf $(PROG) *.o *.dSYM *.gcov *.gcno *.gcda *.obj *.exe *.ilk *.pdb core core.* temp.* *.out
