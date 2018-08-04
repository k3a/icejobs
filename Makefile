LDLIBS += -licecc -lpthread
LDFLAGS += -L/usr/lib/icecream/lib
CFLAGS += -I/usr/lib/icecream/include
CC = g++

icejobs: icejobs.cc
	$(CC) $(LDFLAGS) $(LDLIBS) $(CFLAGS) $< -o $@
	strip $@
