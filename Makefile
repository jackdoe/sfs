MYSQL_CONFIG=mysql_config
PKG_CONFIG=pkg-config
CC=gcc
DEBUG ?= 0
all:
	$(CC) -O2 -DDEBUG=$(DEBUG) -DFUSE_USE_VERSION=26 -Wall `$(MYSQL_CONFIG) --cflags --libs` `$(PKG_CONFIG)  fuse --cflags --libs` -o sfs fs.c entry.c

