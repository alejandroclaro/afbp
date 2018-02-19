#
#  Firewall ByPasser Makefile
#
#  Copyright  2006  Alejandro Claro
#  Email ap0lly0n@users.sourceforge.net
#

# use "gcc" to compile source files.
CC = gcc

# the linker is also "gcc". 
LD = $(CC)

# Compiler flags go here.
CFLAGS = -c -O2 -W -Wall -ansi

# Linker flags go here.
LDFLAGS = -lpthread -lcurses

# directories for the include files and source files.
INCLUDE = .
SRCDIR = ./src/

# list of source fiels and generated files.
SOURCES=$(SRCDIR)fbpss.c $(SRCDIR)http.c $(SRCDIR)tunnel-io.c $(SRCDIR)choose-proxy.c $(SRCDIR)curses-ui.c $(SRCDIR)cmdline.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=fbpss

# top-level rule to create the program.
all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	@echo -e [\\033[1m\\033[33mLD\\033[0m] "$(OBJECTS) ->" \\033[1m\\033[33m$@\\033[0m
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@ 

.c.o:
	@echo -e [\\033[1m\\033[32mCC\\033[0m] "$< -> $@"
	@$(CC) $(CFLAGS) $< -o $@


# cleaning everything that can be automatically recreated with "make".
clean:
	-rm -f $(SRCDIR)*.o
