CC ?= cc
SDL_CONFIG ?= sdl-config
override CPPFLAGS += -I. $(shell $(SDL_CONFIG) --cflags)
CFLAGS ?= -Os
override CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
          -Wstrict-prototypes -Wmissing-prototypes -Wconversion \
          -ffunction-sections -fdata-sections
override LDFLAGS += -Wl,--gc-sections
override LDLIBS += $(shell $(SDL_CONFIG) --libs) -lSDL_ttf
SOURCES = main.c \
          core/util.c core/config.c core/library.c core/session.c \
          core/overrides.c core/metadata.c core/runner.c \
          platform/platform.c platform/tools.c ui/state.c ui/ui.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean
all: forgeshell

forgeshell: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c forgeshell.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) forgeshell

ui/ui.o ui/state.o: ui/state.h
