PURPLE_INCLUDES = `pkg-config --libs --cflags purple sqlite3`

CC ?= gcc
CFLAGS += -O2 -Wall -fpic -fpie -g -pipe
LDFLAGS += -shared -fPIC -Wl,-z,relro,-z,now


.PHONY: all

all: sqlite-log.so

sqlite-log.so: sqlite-log.c
	$(CC) sqlite-log.c $(CFLAGS) $(PURPLE_INCLUDES) $(LDFLAGS) -o sqlite-log.so

install: sqlite-log.so
	install -m 444 sqlite-log.so $(DESTDIR)`$(PKG_CONFIG) --variable=plugindir purple`

uninstall:
	rm -f $(DESTDIR)`$(PKG_CONFIG) --variable=plugindir purple`/sqlite-log.so

clean:
	rm -f sqlite-log.so
