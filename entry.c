#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
//#include <errno.h>
//#include <fcntl.h>
#include <my_global.h>
#include <mysql.h>

#include "common.h"
#include "entry.h"
MYSQL *conn;
char *identifier;
static inline void entry_list_append(struct entry_list *el, struct entry *e);

static struct entry *entry_new(void) {
	struct entry *d = malloc(sizeof(*d));
	if (!d)
		clean_exit(EXIT_FAILURE);
	bzero(d,sizeof(*d));
	return d;
}
static int entry_set_times(struct entry *e) {
	assert(e->id > 0);
	char query[512];	
	QUERY("UPDATE `entries_%s` SET atime = %llu,mtime = %llu,ctime = %llu, btime = %llu WHERE id=%llu",identifier,e->atime,e->mtime,e->ctime,e->btime,e->id);
	sql_do(query);
	return 0;
	
}

static int entry_set_stat(struct entry *e) {
	assert(e->path && e->id > 0);
	char query[512];	
	QUERY("UPDATE `entries_%s` SET path = '%s', obj_type = %llu, uid = %u, gid = %u, mode = CONV(%u,10,8),ctime = %u WHERE id=%llu",identifier,e->path,e->obj_type,(uid_t)e->uid,(gid_t)e->gid,(mode_t)e->mode,(unsigned int)time(NULL),e->id);
	sql_do(query);
	return 0;
}

static int entry_set_data(struct entry *e) {
	assert(e->id > 0);
	unsigned int t = (unsigned int) time(NULL);
	char query[512];
	if (e->len > 0) {
		/* POC */
		QUERY("UPDATE `entries_%s` SET data = ?,mtime = %u WHERE id = %llu",identifier,t,e->id);
		_D("query: %s",query);
		MYSQL_BIND  bind[1];
		MYSQL_STMT  *stmt;
		bzero(bind,sizeof(bind));
		stmt = mysql_stmt_init(conn);
		mysql_stmt_prepare(stmt, query, strlen(query));
		bind[0].buffer_type = MYSQL_TYPE_BLOB;
		bind[0].buffer = e->data;
		bind[0].buffer_length = e->len;
		bind[0].is_null = 0;
		bind[0].length = (unsigned long *) &e->len;
		mysql_stmt_bind_param(stmt, bind);
		mysql_stmt_execute(stmt);
		mysql_stmt_affected_rows(stmt);
		mysql_stmt_close(stmt);
		return 0;
	} else {
		QUERY("UPDATE `entries_%s` SET data = NULL,mtime = %u WHERE id = %llu",identifier,t,e->id);
		sql_do(query);
		return 0;
	}
}
void entry_destroy(struct entry *d) {
	assert(d);
	if (d->data)
		free(d->data);
	if (d->path)
		free(d->path);
	free(d);
}
struct entry *entry_create(const char *path,const char *extra) {
	char query[512];
	unsigned int t = (unsigned int) time(NULL);
	if (extra)
		QUERY("INSERT INTO `entries_%s` (path,data,btime,ctime,mtime,atime) VALUES('%s','%s',%u,%u,%u,%u)",identifier,path,extra,t,t,t,t);
	else
		QUERY("INSERT INTO `entries_%s` (path,btime,ctime,mtime,atime) VALUES('%s',%u,%u,%u,%u)",identifier,path,t,t,t,t);
		
	if (sql_do(query) != 1)
		return NULL;
	return entry_get(path,0,0,0,EMPTY_DATA);
}

int entry_save(struct entry *e,int what) {
	switch(what) {
	case SAVE_TIME:
		return entry_set_times(e);
	break;
	case SAVE_DATA:
		return entry_set_data(e);
	break;
	case SAVE_STAT:
		return entry_set_stat(e);
	break;
	}
	return -ENOENT;
}

struct entry *entry_get(const char *path,size_t size, off_t offset,size_t min_size,int fill) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	unsigned long *lens;
	char query[512];
	struct entry *d = NULL;
	if (size == 0 && offset == 0) {
		QUERY("SELECT id,data,obj_type,uid,gid,CONV(mode,8,10),ctime,mtime,atime,btime FROM `entries_%s` WHERE path = '%s'",identifier,path);
	} else {
		QUERY("SELECT id,SUBSTRING(data,%u,%u),obj_type,uid,gid,CONV(mode,8,10),ctime,mtime,atime,btime FROM `entries_%s` WHERE path = '%s'",(unsigned int) offset + 1,(unsigned int)size,identifier,path);
	}
	sql_do(query);
	result = mysql_store_result(conn);
	if (result) {
		if ((row = mysql_fetch_row(result))) {
			d = entry_new();
			lens = mysql_fetch_lengths(result);
			d->len = 0;
			d->id = atoi(row[0]);
			d->obj_type = atoi(row[2]);
			d->path = strdup(path);
			d->uid = atoi(row[3]);
			d->gid = atoi(row[4]);
			d->mode = atoi(row[5]);
			d->ctime = atoi(row[6]);
			d->mtime = atoi(row[7]);
			d->atime = atoi(row[8]);
			d->btime = atoi(row[8]);
			if (row[1]) {
				d->len = lens[1];
				if (fill == FILL_DATA) {
					d->data = malloc(max(d->len + 1,min_size));
					if (d->data) {
						COPY(row[1],d->data,d->len);					
					} else {
						_E("not enough mem to allocate: %llu",max(d->len,min_size));
						clean_exit(EXIT_FAILURE);
						/* XXX: 
						 * dont really know what to do here 
						 * return NULL? or d->len = 0? or just exit?
						 */
					}
				}
			}
		}
		mysql_free_result(result);
	}
	return d;
}
int is_type(const char *path, int type) {
	struct entry *d = entry_get(path,0,0,0,EMPTY_DATA);
	if (!d) 
		return 0;
	if (type != OBJ_ANY && d->obj_type != type) {
		entry_destroy(d);
		return 0;
	}
	entry_destroy(d);
	return 1;
}

void create_root_if_needed(void) {
	struct entry *e = entry_get("/",0,0,0,EMPTY_DATA);
	if (!e) {
		e = entry_create("/",NULL);
		if (!e) {
			_E("fail to create root directory");
			clean_exit(EXIT_FAILURE);
		}		
		e->obj_type = OBJ_DIR;
		e->uid = getuid();
		e->gid = getgid();
		e->mode = 0755;
		entry_save(e,SAVE_STAT);
	} else if (e->obj_type != OBJ_DIR) {
		e->obj_type = OBJ_DIR;
		entry_save(e,SAVE_STAT);
	}
	entry_destroy(e);
}


int entry_delete(struct entry *e) {
	assert(e->id > 0 && e->path);
	char query[512];
	if (e->obj_type == OBJ_DIR) {
		QUERY("SELECT count(path) FROM `entries_%s` WHERE path LIKE '%s%%'",identifier,e->path);
		if (sql_count(query) != 1)
			return -ENOTEMPTY;
	}
	QUERY("DELETE FROM entries WHERE id = %llu",e->id);
	sql_do(query);
	return 0;
}


int entry_rename(struct entry *e, const char *dest) {
	assert(e->id > 0 && e->path);
	char query[512];
	QUERY("UPDATE `entries_%s` SET path = REPLACE(path,'%s','%s') WHERE path LIKE '%s%%'",identifier,e->path,dest,e->path);
	sql_do(query);
	return 0;
}
static inline void entry_list_append(struct entry_list *el, struct entry *e) {
	if (el->head == NULL)
		el->head = e;
	else
		el->tail->next = e;
	el->tail = e;
	e->next = NULL;
}

void entry_list_fill(struct entry_list *el,char *query, int fill) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	sql_do(query);
	result = mysql_store_result(conn);
	if (result) {		
		while ((row = mysql_fetch_row(result))) {
			if (row[0]) {
				struct entry *e = entry_new();
				e->path = strdup(row[0]);
				entry_list_append(el,e);
				el->count++;
			}
		}
		mysql_free_result(result);
		return;
	}
	el->count = -ENOENT;
}
void entry_list_init(struct entry_list *el) {
	el->head = el->tail = NULL;
	el->count = 0;
}
void entry_list_destroy(struct entry_list *el) {
	struct entry *e;
	while ((e = el->head)) {
		el->head = e->next;
		entry_destroy(e);
	}
}


uint64_t sql_do(char *query) {
	_D("query: %s",query);
	if (mysql_query(conn, query) < 0) {
		_E("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
	}
	return mysql_affected_rows(conn);
}

char *sql_sanitize(const char *s) {
	unsigned int len = strlen(s);
	/* 
	 * You must allocate the to buffer to be at least length*2+1 bytes long
	 * http://dev.mysql.com/doc/refman/4.1/en/mysql-real-escape-string.html
	 */ 
	char *to = malloc((len * 2) + 1); 
	if (!to) {
		_E("no mem for %u bytes",(len * 2) + 1);
		clean_exit(EXIT_FAILURE);
	}
	mysql_real_escape_string(conn,to,s,len);
	return to;
}
uint64_t sql_count(char *query) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	uint64_t count = 0;
	sql_do(query);
	result = mysql_store_result(conn);
	if (result) {
		if ((row = mysql_fetch_row(result))) {
			count = (atoi(row[0]) == 1) ? 1 : 0;
		}
		mysql_free_result(result);
	}
	return count;
}
void sql_connect(void) {
	conn = mysql_init(NULL);
	if (conn == NULL) {
		_E("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
		exit(EXIT_FAILURE);
	}
	if (mysql_real_connect(conn, _MYSQL_HOST, _MYSQL_USER,_MYSQL_PASS, _MYSQL_DATABASE, _MYSQL_PORT, NULL, 0) == NULL) {
		_E("Error %u: %s\n", mysql_errno(conn), mysql_error(conn));
		exit(EXIT_FAILURE);
	}
	identifier = _MYSQL_USER;
	char query[1024];
	
	QUERY("CREATE TABLE IF NOT EXISTS `entries_%s` (\
	`id` bigint(20) NOT NULL AUTO_INCREMENT,	\
	`path` varchar(255) NOT NULL,			\
	`data` longblob,				\
	`obj_type` int(11) NOT NULL,			\
	`uid` bigint(20) NOT NULL,			\
	`gid` bigint(20) NOT NULL,			\
	`mode` bigint(20) NOT NULL,			\
	`atime` bigint(20) NOT NULL,			\
	`mtime` bigint(20) NOT NULL,			\
	`ctime` bigint(20) NOT NULL,			\
	`btime` bigint(20) NOT NULL,			\
	PRIMARY KEY (`id`),				\
	UNIQUE KEY `path` (`path`)) 			\
	ENGINE=InnoDB AUTO_INCREMENT=2264 DEFAULT CHARSET=utf8",identifier);
	sql_do(query);
	create_root_if_needed();
	
}
void sql_close(void) {
	mysql_close(conn);
}