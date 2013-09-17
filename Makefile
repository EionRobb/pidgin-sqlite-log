
.PHONY:	all clean install

all: sqlite-log.so

sqlite-log.so: sqlite-log.c
	gcc `pkg-config --cflags --libs purple glib-2.0 sqlite3` -g -O2 -fPIC -pipe sqlite-log.c -o sqlite-log.so -shared

clean:
	rm -f sqlite-log.so

install:
	cp sqlite-log.so ~/.purple/plugins/
