GTK_PIDGIN_INCLUDES= `pkg-config --cflags gtk+-2.0 pidgin`

CC ?= clang
CFLAGS+= -O2 -Wall -fpic -fpie -g -pipe
LDFLAGS+= -shared -fPIC -Wl,-z,relro,-z,now
PREFIX=/usr
SOPATH=/lib64/pidgin

INCLUDES = \
      $(GTK_PIDGIN_INCLUDES)

sqlite-log.so: sqlite-log.c
	$(CC) sqlite-log.c $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o sqlite-log.so

install: sqlite-log.so
	mkdir -p ${DESTDIR}${PREFIX}${SOPATH}
	chmod 444  ${DESTDIR}${PREFIX}${SOPATH}

	install -m 444 sqlite-log.so ${DESTDIR}${PREFIX}${SOPATH}

uninstall:
	rm -f ~/.purple/plugins/sqlite-log.so

clean:
	rm -f sqlite-log.so
