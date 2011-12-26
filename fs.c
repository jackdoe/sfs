#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include "common.h"
#include "entry.h"

int file_create(const char *path,int file_type, uid_t u, gid_t g, mode_t m, const char *extra) {
	int rc = 0;
	Entry *e = new Entry(path);
	if (e->valid()) 
		rc = -EEXIST;
	
	else if (e->create(extra) == 0) {
		e->setObj_type(file_type);
		e->chown(u,g);
		e->chmod(m);
		rc = e->save();
	} else 
		rc = -EIO;
	delete e;
	return rc;
}
int file_readdir(const char *path, void *buf, fuse_fill_dir_t filler) {
	Entry e = Entry(path);
	return e.readdir(buf,filler);
}	
static int sfs_getattr(const char *path, struct stat *stbuf) {
	Entry e = Entry(path);
	return e.to_stat(stbuf);
}

static int sfs_mkdir(const char *path, mode_t mode) {
	return file_create(path,OBJ_DIR,getuid(),getgid(),mode,NULL);
}

static int sfs_rmdir(const char * path) {
	Entry e = Entry(path);
	return e.destroy();
}

static int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	Entry e = Entry(path);
	return e.readdir(buf,filler);
}
	
static int sfs_open(const char *path, struct fuse_file_info *fi) {
	return 0;
}

static int sfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	Entry e = Entry(path);
	e.write(buf,size,offset);
	return size;
}
static int sfs_truncate(const char *path , off_t offset) {
	Entry e = Entry(path);
	return e.write(NULL,0,0);
}
static int sfs_read(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi) {
	Entry e = Entry(path);
	return e.read(buf,size,offset);
}
static int sfs_readlink(const char *path, char *buf, size_t size) {
	Entry e = Entry(path);
	return (e.read(buf,size,0) > 0 ? 0 : -ENOENT);
}

static int sfs_create (const char *path, mode_t mode, struct fuse_file_info * fi) {
	return file_create(path,OBJ_FILE,getuid(),getgid(),mode,NULL);
}

static int sfs_delete(const char * path) {
	Entry e = Entry(path);
	return e.destroy();
}
static int sfs_chmod(const char *path, mode_t mode) {
	Entry e = Entry(path);
	e.chmod(mode);
	return e.save();
}

static int sfs_chown(const char *path, uid_t u, gid_t g) {
	Entry e = Entry(path);
	e.chown(u,g);
	return e.save();
}
static int sfs_utimens(const char *path,const struct timespec ts[2]) {
	Entry e = Entry(path);
	e.utimens(ts);
	return e.save();
}

static int sfs_symlink(const char *data, const char *path) {
	return file_create(path,OBJ_LINK,getuid(),getgid(),0777,data);
}

static int sfs_rename(const char *path, const char *dest) {
	Entry e = Entry(path);
	return e.rename(dest);
}


static int sfs_statfs(const char *path , struct statvfs *s) {
	s->f_bsize=(1U << 16);
	s->f_frsize=s->f_bsize;

	s->f_bfree=(1U << 24);
	s->f_ffree=s->f_bfree;
	
	s->f_bavail=(1U << 24);
	s->f_favail=s->f_bavail;
	s->f_blocks = s->f_bavail;	
	return 0;
}

static struct fuse_operations sfs_oper;

int main(int argc, char *argv[])
{
	SQL_INIT();
	sfs_oper.flag_nullpath_ok = 0;
	sfs_oper.getattr	= sfs_getattr;
	sfs_oper.readdir	= sfs_readdir;
	sfs_oper.open		= sfs_open;
	sfs_oper.write		= sfs_write;
	sfs_oper.truncate	= sfs_truncate;
	sfs_oper.read		= sfs_read;
	sfs_oper.create		= sfs_create;
	sfs_oper.unlink		= sfs_delete;
	sfs_oper.chmod		= sfs_chmod;
	sfs_oper.chown		= sfs_chown;
	sfs_oper.statfs		= sfs_statfs;
	sfs_oper.mkdir		= sfs_mkdir;
	sfs_oper.readlink	= sfs_readlink;
	sfs_oper.symlink	= sfs_symlink;
	sfs_oper.rename		= sfs_rename;
	sfs_oper.rmdir		= sfs_rmdir;
	sfs_oper.utimens	= sfs_utimens;

	return fuse_main(argc, argv, &sfs_oper, NULL);
}

void clean_exit(int rc) {
	SQL_DESTROY();
	exit(rc);
}
