MYSQL_CONFIG=mysql_config
PKG_CONFIG=pkg-config
CC=g++
DEBUG ?= 0
all:
	$(CC) -O2 -DDEBUG=$(DEBUG) -DFUSE_USE_VERSION=26 -Wall `$(MYSQL_CONFIG) --cflags --libs --include` `$(PKG_CONFIG)  fuse --cflags --libs` -o sfs fs.c entry.c

