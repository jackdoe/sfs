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

pthread_mutex_t giant;

int file_utimens(const char *path, struct timespec const ts[2]) {
	struct entry *e = entry_get(path,0,0,0,EMPTY_DATA);
	if (!e)
		return -ENOENT;
	e->atime = ts[0].tv_sec;
	e->mtime = ts[1].tv_sec;
	int rc = entry_save(e,SAVE_TIME);
	entry_destroy(e);
	return rc;
}

int file_modify(const char *path,int action, int file_type, uid_t u, gid_t g, mode_t m, const char *extra) {
	struct entry *e;	
	int rc = 0;
	switch (action) {
	case ACTION_CREATE:
		if ((e = entry_create(path,extra)) == NULL)
			return -EEXIST;
		
		e->gid = g;
		e->uid = u;
		e->obj_type = file_type;
		e->mode = m;
		rc = entry_save(e,SAVE_STAT);
		entry_destroy(e);
	break;
	case ACTION_CHMOD:
		e = entry_get(path,0,0,0,EMPTY_DATA);
		if (!e)
			return -ENOENT;
		e->mode = m;
		rc = entry_save(e,SAVE_STAT);
		entry_destroy(e);
	break;
	case ACTION_CHOWN:
		e = entry_get(path,0,0,0,EMPTY_DATA);
		if (!e)
			return -ENOENT;
		e->uid = u;
		e->gid = g;
		rc = entry_save(e,SAVE_STAT);		
		entry_destroy(e);
	break;
	case ACTION_DELETE:
		if (!EXISTS(path)) 
			return -ENOENT;
		e = entry_get(path,0,0,0,EMPTY_DATA);
		if (!e)
			return -ENOENT;
		rc = entry_delete(e);
		entry_destroy(e);
	break;
	case ACTION_RENAME:
		if (IS_ROOT(path) || !EXISTS(path))
			return -ENOENT;
		if (EXISTS(extra))
			return -EEXIST;
		e = entry_get(path,0,0,0,EMPTY_DATA);
		rc = entry_rename(e,extra);
		entry_destroy(e);
		
	break;
	}
	return rc;
}


int file_write(const char *path, const char *buf, size_t size, off_t offset) {
	struct entry *e = entry_get(path,0,0,size+offset,FILL_DATA);
	if (!e) 
		return -ENOENT;
	if (!e->data) 
		e->data = malloc(size + offset);
	
	COPY(buf,(e->data + offset),size);
	e->len = size + offset;
	entry_save(e,SAVE_DATA);
	entry_destroy(e);
	return size;
}
static int file_read(const char *path, char *buf, size_t size, off_t offset) {
	struct entry *e = entry_get(path,size,offset,0,FILL_DATA);
	if (!e)
		return -ENOENT;
	if (e->data) 
		COPY(e->data,buf,e->len);
	if (e->obj_type == OBJ_LINK)  /* XXX */
		buf[min(e->len,size)] = '\0';
	
	size = e->len;
	entry_destroy(e);
	return size;
}

int file_getattr(const char *path, struct stat *stbuf) {
	struct entry *d = entry_get(path,0,0,0,EMPTY_DATA);
	if (!d) 
		return -ENOENT;

	bzero(stbuf,sizeof(*stbuf));
	stbuf->st_nlink = 1;		
	stbuf->st_uid = d->uid;
	stbuf->st_gid = d->gid;
	stbuf->st_ino = d->id;
	stbuf->st_atime = d->atime;
	stbuf->st_mtime = d->mtime;
	stbuf->st_ctime = d->ctime;	
	switch(d->obj_type) {
	case OBJ_FILE:
		stbuf->st_mode = S_IFREG | d->mode;
		stbuf->st_size = d->len;		
	break;
	case OBJ_LINK:
		stbuf->st_mode = S_IFLNK | d->mode;
	break;
	case OBJ_DIR:
		stbuf->st_mode = S_IFDIR | d->mode;
	break;
	}
	entry_destroy(d);
	return 0;
}
int file_readdir(const char *path, void *buf, fuse_fill_dir_t filler) {
	char query[512];
	if (IS_ROOT(path)) {
		QUERY("SELECT path FROM `entries_%s` WHERE path REGEXP '^/.[^/]*$'",identifier);
	} else {
		QUERY("SELECT path FROM `entries_%s` WHERE path REGEXP '^%s/[^/]*$'",identifier,path);
	}
	struct entry_list el;
	struct entry *e;
	entry_list_init(&el);
	entry_list_fill(&el,query,EMPTY_DATA);
	if (el.count < 0) 
		return -ENOENT;
	
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while ((e = el.head)) {
		el.head = e->next;
		filler(buf, strdup(strrchr(e->path,'/') + 1), NULL, 0);
	}
	entry_list_destroy(&el);
	return 0;
}

#define OPERATION(fx,va...) 						\
	_D("%s",path);							\
	pthread_mutex_lock(&giant);					\
	char *p = sql_sanitize(path);					\
	int rc = fx(p,##va);						\
	pthread_mutex_unlock(&giant);					\
	free(p);
	
static int sfs_getattr(const char *path, struct stat *stbuf) {
	OPERATION(file_getattr,stbuf);
	return rc;
}

static int sfs_mkdir(const char *path, mode_t mode) {
	OPERATION(file_modify,ACTION_CREATE,OBJ_DIR,getuid(),getgid(),mode,NULL);
	return rc;
}

static int sfs_rmdir(const char * path) {
	OPERATION(file_modify,ACTION_DELETE,0,0,0,0,NULL);
	return rc;
}

static int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	OPERATION(file_readdir,buf,filler);
	return rc;
}
	
static int sfs_open(const char *path, struct fuse_file_info *fi) {
	return 0;
}

static int sfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	OPERATION(file_write,buf,size,offset);
	return rc;
}
static int sfs_truncate(const char *path , off_t offset) {
	OPERATION(file_write,NULL,0,0);
	return rc;
}
static int sfs_read(const char *path, char *buf, size_t size, off_t offset,struct fuse_file_info *fi) {
	OPERATION(file_read,buf,size,offset);
	return rc;
}
static int sfs_readlink(const char *path, char *buf, size_t size) {
	OPERATION(file_read,buf,size,0);
	return (rc > 0) ? 0 : -ENOENT;
}

static int sfs_create (const char *path, mode_t mode, struct fuse_file_info * fi) {
	OPERATION(file_modify,ACTION_CREATE,OBJ_FILE,getuid(),getgid(),mode,NULL);
	return rc;
}

static int sfs_delete(const char * path) {
	OPERATION(file_modify,ACTION_DELETE,0,0,0,0,NULL);
	return rc;
}
static int sfs_chmod(const char *path, mode_t mode) {
	OPERATION(file_modify,ACTION_CHMOD,0,0,0,mode,NULL);
	return rc;
}

static int sfs_chown(const char *path, uid_t u, gid_t g) {
	OPERATION(file_modify,ACTION_CHMOD,0,u,g,0,NULL);
	return rc;
}
static int sfs_utimens(const char *path,const struct timespec ts[2]) {
	OPERATION(file_utimens,ts);
	return rc;
}

static int sfs_symlink(const char *data, const char *path) {
	char *d = sql_sanitize(data);
	OPERATION(file_modify,ACTION_CREATE,OBJ_LINK,getuid(),getgid(),0777,d);
	free(d);
	return rc;
}

static int sfs_rename(const char *path, const char *dest) {
	char *d = sql_sanitize(dest);
	OPERATION(file_modify,ACTION_RENAME,0,0,0,0,d);
	free(d);
	return rc;
}

#undef OPERATION

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

static struct fuse_operations sfs_oper = {
	.getattr	= sfs_getattr,
	.readdir	= sfs_readdir,
	.open		= sfs_open,
	.write		= sfs_write,
	.truncate	= sfs_truncate,
	.read		= sfs_read,
	.create		= sfs_create,
	.unlink		= sfs_delete,
	.chmod		= sfs_chmod,
	.chown		= sfs_chown,
	.statfs		= sfs_statfs,
	.mkdir		= sfs_mkdir,
	.readlink	= sfs_readlink,
	.symlink	= sfs_symlink,
	.rename		= sfs_rename,
	.rmdir		= sfs_rmdir,
	.utimens	= sfs_utimens,
};

int main(int argc, char *argv[])
{
	pthread_mutex_init(&giant,NULL);
	sfs_oper.flag_nullpath_ok = 0;
	sql_connect();	
	return fuse_main(argc, argv, &sfs_oper, NULL);
}

void clean_exit(int rc) {
	sql_close();
	exit(rc);
}
