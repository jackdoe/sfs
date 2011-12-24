MYSQL_CONFIG=mysql_config
PKG_CONFIG=pkg-config
CC=gcc

all:
	$(CC) -O2 -DFUSE_USE_VERSION=26 -Wall `$(MYSQL_CONFIG) --cflags --libs` `$(PKG_CONFIG)  fuse --cflags --libs` -o sfs fs.c entry.c

