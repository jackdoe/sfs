#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "entry.h"
#define Q(arg...) snprintf(_query,MAX_Q_SIZE,##arg)
#define THIS_PATH(arg...) snprintf(_path,PATH_MAX,##arg)

char *identifier;
struct connection GIANT;
static void *zalloc(uint64_t len) {
	void *p = malloc(len);
	if (!p) 
		SAYX(EXIT_FAILURE,"failed to allocate: %llu bytes",len);
	
	return (void *) p;
}
Entry::Entry(const char *path_unescaped) {
	_c = &GIANT;
	pthread_mutex_lock(&_c->lock);
	if (path_unescaped) {
		char *escape = this->SQL_Escape(path_unescaped);	
		this->load(escape);
		free(escape);
	}

}
Entry::~Entry(void) {
	pthread_mutex_unlock(&_c->lock);
}

int Entry::save(void) {
	if (!this->valid()) 
		return -ENOENT;
	Q("UPDATE `entries_%s` SET 	\
	obj_type = %llu, 		\
	uid = %llu, gid = %llu, 	\
	mode = CONV(%llu,10,8), 	\
	atime = %llu,			\
	mtime = %llu,			\
	ctime = %llu, 			\
	btime = %llu 			\
	WHERE id=%llu",
		identifier,
		_obj_type,
		_u.uid,_u.gid,
		_u.mode,
		_t.atime,_t.mtime,_t.ctime,_t.btime,_id);
		
	this->SQL_Execute();
	return 0;
}
void Entry::load(const char *p) {
	_id = 0;
	_validated = false;
	THIS_PATH("%s",p);
	MYSQL_RES *result;
	MYSQL_ROW row;
	Q("SELECT id,obj_type,uid,gid,CONV(mode,8,10),ctime,mtime,atime,btime,LENGTH(data) FROM `entries_%s` WHERE path = '%s'",identifier,p);
	this->SQL_Execute();
	result = mysql_store_result(_c->conn);
	if (result) {
		if ((row = mysql_fetch_row(result))) {
			_id = this->atoi(row[0]);
			_obj_type = this->atoi(row[1]);
			_u.uid = this->atoi(row[2]);
			_u.gid = this->atoi(row[3]);
			_u.mode = this->atoi(row[4]);
			_t.ctime = this->atoi(row[5]);
			_t.mtime = this->atoi(row[6]);
			_t.atime = this->atoi(row[7]);
			_t.btime = this->atoi(row[8]);
			_data_len = this->atoi(row[9]);
			_validated = true;
		}
		mysql_free_result(result);	
	}
}

uint64_t Entry::atoi(char *str) {
	/* validator */
	if (!str)
		return 0;
	return strtoll(str, (char **)NULL, 10);
}
bool Entry::valid(void) {
	return (_validated == true);
}
int Entry::create(const char *extra_unescaped) {
	unsigned int tim = (unsigned int) time(NULL);
	if (extra_unescaped) {
		char *escape = this->SQL_Escape(extra_unescaped);	
		Q("INSERT INTO `entries_%s` (path,data,btime,ctime,mtime,atime) VALUES('%s','%s',%u,%u,%u,%u)",identifier,_path,escape,tim,tim,tim,tim);
		free(escape);
	} else
		Q("INSERT INTO `entries_%s` (path,btime,ctime,mtime,atime) VALUES('%s',%u,%u,%u,%u)",identifier,_path,tim,tim,tim,tim);
	
	if (this->SQL_Execute() == 1) {
		this->load(_path);
		return 0;
	}
	return -EEXIST;
}
int Entry::destroy(void) {
	if (!this->valid()) 
		return -ENOENT;
	if (_obj_type == OBJ_DIR) {
		Q("SELECT count(path) FROM `entries_%s` WHERE path LIKE '%s%%'",identifier,_path);
		if (this->SQL_Count() != 1)
			return -ENOTEMPTY;
	}
	Q("DELETE FROM `entries_%s` WHERE id = %llu",identifier,_id);
	this->SQL_Execute();
	return 0;
}

int Entry::rename(const char *to_unescaped) {
	if (!this->valid())
		return -ENOENT;
	if (!to_unescaped)
		return -EIO;
	char *to = this->SQL_Escape(to_unescaped);
	Q("UPDATE `entries_%s` SET path = REPLACE(path,'%s','%s') WHERE path LIKE '%s%%'",identifier,_path,to,_path);
	free(to);
	this->SQL_Execute();
	THIS_PATH("%s",to);
	return 0;
}
int Entry::chown(uid_t uid, gid_t gid) {
	if (!this->valid()) 
		return -ENOENT;
	_t.ctime = time(NULL);
	_u.uid = uid;
	_u.gid = gid;
	return 0;
}
int Entry::chmod(mode_t mode) {
	if (!this->valid()) 
		return -ENOENT;
	_t.ctime = time(NULL);
	_u.mode = mode;
	return 0;
}

int Entry::utimens(struct timespec const ts[2]) {
	if (!this->valid()) 
		return -ENOENT;
	_t.atime = ts[0].tv_sec;
	_t.mtime = ts[1].tv_sec;
	_t.ctime = time(NULL);
	return 0;
}

int Entry::write(const char *buf,size_t size, off_t offset) {
	if (!this->valid()) 
		return -ENOENT;

	/* XXX: hack it - n^2 unberably slow with data > 1mb! */
	char *copy = NULL;
	uint64_t copy_len = size;
	if (offset > 0) {
		MYSQL_RES *result;
		MYSQL_ROW row;
		unsigned long *lens;
		Q("SELECT data FROM `entries_%s` WHERE id = %llu",identifier,_id);
		this->SQL_Execute();		
		result = mysql_store_result(_c->conn);
		if (result) {
			if ((row = mysql_fetch_row(result))) {
				if (row[0]) {
					lens = mysql_fetch_lengths(result);			
					copy_len += lens[0];
					copy = (char *) zalloc(copy_len);
					COPY(row[0],copy,lens[0]);
				}
			}
			mysql_free_result(result);
		}
	}
	if (!copy)
		copy = (char *) zalloc(size+offset);
	COPY(buf,(copy+offset),size);
	Q("UPDATE `entries_%s` SET data = ?,mtime = %u WHERE id = %llu",identifier,(unsigned int) time(NULL),_id);
	_D("query: %s",_query);
	MYSQL_BIND  bind[1];
	MYSQL_STMT  *stmt;
	bzero(bind,sizeof(bind));
	stmt = mysql_stmt_init(_c->conn);
	if (!stmt) 
		SAYX(EXIT_FAILURE,"no mem for stmt");
	
	if (mysql_stmt_prepare(stmt, _query, strlen(_query))) 
		SAYX(EXIT_FAILURE,"failed to prepare stmt: %s",mysql_stmt_error(stmt));
	
	bind[0].buffer_type = MYSQL_TYPE_BLOB;
	bind[0].buffer = copy;
	bind[0].buffer_length = copy_len;
	bind[0].is_null = 0;
	bind[0].length = (unsigned long *) &copy_len;
	mysql_stmt_bind_param(stmt, bind);
	if (mysql_stmt_execute(stmt))
		SAYX(EXIT_FAILURE,"failed to execute stmt: %s",mysql_stmt_error(stmt));
	if(mysql_stmt_close(stmt))
		SAYX(EXIT_FAILURE,"failed to close stmt: %s",mysql_stmt_error(stmt));
	return 0;
}
int Entry::read(char *buf,size_t size,off_t offset) {
	if (!this->valid()) 
		return -ENOENT;
	MYSQL_RES *result;
	MYSQL_ROW row;
	unsigned long *lens;
	uint64_t len = 0;
	Q("SELECT SUBSTRING(data,%u,%u) FROM `entries_%s` WHERE id = %llu",(unsigned int) offset + 1,(unsigned int)size,identifier,_id);
	this->SQL_Execute();	
	result = mysql_store_result(_c->conn);
	if (result) {
		if ((row = mysql_fetch_row(result))) {
			if (row[0]) {
				lens = mysql_fetch_lengths(result);
				len = min(size,lens[0]);
				COPY(row[0],buf,len);
				if (this->_obj_type == OBJ_LINK) 
					buf[min(len,size)] = '\0';
			}
		}
		mysql_free_result(result);
	}
	return len;
}

int Entry::readdir(void *buf, fuse_fill_dir_t filler) {
	if (!this->valid()) 
		return -ENOENT;
	int rc = -ENOENT;
	MYSQL_RES *result;
	MYSQL_ROW row;

	if (IS_ROOT(_path)) {
		Q("SELECT path FROM `entries_%s` WHERE path REGEXP '^/.[^/]*$'",identifier);
	} else {
		Q("SELECT path FROM `entries_%s` WHERE path REGEXP '^%s/[^/]*$'",identifier,_path);
	}
	this->SQL_Execute();
	result = mysql_store_result(_c->conn);
	if (result) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);	
		while ((row = mysql_fetch_row(result))) {
			if (row[0])
				filler(buf,strrchr(row[0],'/') + 1,NULL,0);
			
		}
		mysql_free_result(result);
		rc = 0;
	}
	return rc;
}

int Entry::to_stat(struct stat *s) {
	if (!this->valid()) 
		return -ENOENT;
	bzero(s,sizeof(*s));
	s->st_nlink = 1;
	s->st_uid = _u.uid;
	s->st_gid = _u.gid;
	s->st_ino = _id;
	s->st_atime = _t.atime;
	s->st_mtime = _t.mtime;
	s->st_ctime = _t.ctime;	
	switch(_obj_type) {
	case OBJ_FILE:
		s->st_mode = S_IFREG | _u.mode;
		s->st_size = _data_len;		
	break;
	case OBJ_LINK:
		s->st_mode = S_IFLNK | _u.mode;
	break;
	case OBJ_DIR:
		s->st_mode = S_IFDIR | _u.mode;
	break;
	}
	return 0;
}
void create_root_if_needed(void) {
	Entry *e = new Entry("/");
	e->create_table_if_needed();
	if (!e->valid()) {
		if (e->create(NULL) == 0) {
			e->setObj_type(OBJ_DIR);
			e->chown(getuid(),getgid());
			e->chmod(0755);
			e->save();
		} else 
			SAYX(EXIT_FAILURE,"failed to create root directory");
	} else if (e->getObj_type() != OBJ_DIR) {
		e->setObj_type(OBJ_DIR);
		e->save();
	}
	delete e;
}

void Entry::create_table_if_needed(void) {
	Q("CREATE TABLE IF NOT EXISTS `entries_%s` (	\
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
	UNIQUE KEY `path` (`path`))",identifier);
	this->SQL_Execute();
}


char *Entry::SQL_Escape(const char *string) {
	unsigned int len = strlen(string);
	/* 
	 * You must allocate the to buffer to be at least length*2+1 bytes long
	 * http://dev.mysql.com/doc/refman/4.1/en/mysql-real-escape-string.html
	 */ 
	char *to = (char *) zalloc((len * 2) + 1); 
	mysql_real_escape_string(_c->conn,to,string,len);
	return to;
}
void Entry::SQL_Connect(struct connection *where) {
	where->conn = mysql_init(NULL);
	pthread_mutex_init(&where->lock,NULL);

	if (where->conn == NULL) 
		SAYX(EXIT_FAILURE,"Error %u: %s", mysql_errno(where->conn), mysql_error(where->conn));
	if (mysql_real_connect(where->conn, _MYSQL_HOST, _MYSQL_USER,_MYSQL_PASS, _MYSQL_DATABASE, _MYSQL_PORT, NULL, 0) == NULL)
		SAYX(EXIT_FAILURE,"Error %u: %s", mysql_errno(where->conn), mysql_error(where->conn));
	identifier = strdup(_MYSQL_USER);
	create_root_if_needed();	
}
void Entry::SQL_Close(struct connection *where) {
	pthread_mutex_lock(&where->lock);
	if (where->conn) {
		mysql_close(where->conn);
	}
	pthread_mutex_unlock(&where->lock);
	pthread_mutex_destroy(&where->lock);
}

uint64_t Entry::SQL_Execute(void) {
	_D("query: %s",_query);
	if (mysql_query(_c->conn, _query) < 0)
		SAYX(EXIT_FAILURE,"Error %u: %s\n", mysql_errno(_c->conn), mysql_error(_c->conn));
	return mysql_affected_rows(_c->conn);
}
uint64_t Entry::SQL_Count(void) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	uint64_t count = 0;
	this->SQL_Execute();
	result = mysql_store_result(_c->conn);
	if (result) {
		if ((row = mysql_fetch_row(result))) {
			count = this->atoi(row[0]);
		}
		mysql_free_result(result);
	}
	return count;
}

void SQL_INIT(void) {
	Entry::SQL_Connect(&GIANT);
}

void SQL_DESTROY(void) {
	Entry::SQL_Close(&GIANT);
}
