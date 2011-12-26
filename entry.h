#ifndef ENTRY_H
#define ENTRY_H
#include <my_global.h>
#include <mysql.h>
#define MAX_Q_SIZE 	1024
#define MAX_PATH_SIZE	MAX_Q_SIZE/8
#define OBJ_ANY 	0
#define OBJ_FILE 	1
#define OBJ_DIR 	2
#define OBJ_LINK 	3

struct connection {
	MYSQL *conn;
	pthread_mutex_t lock;
};

struct stime {
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint64_t btime;
};
struct ustat {
	uint64_t uid;
	uint64_t gid;
	uint64_t mode;
};

class Entry {
	public:
		Entry(const char *path_unescaped = NULL);
		~Entry(void);
		bool valid(void);
		void setObj_type(uint64_t obj_type) { _obj_type = obj_type; }
		uint64_t getObj_type(void) { return _obj_type; }

		int chown(uid_t uid,gid_t gid);
		int rename(const char *to_unescaped);
		int destroy(void);
		int create(const char *extra_unescaped);
		int chmod(mode_t mode);
		int utimens(struct timespec const ts[2]);
		int to_stat(struct stat *s);
		int readdir(void *buf, fuse_fill_dir_t filler);
		int write(const char *buf,size_t size,off_t offset);
		int read(char *buf,size_t size,off_t offset);
		int save(void);	
		static void SQL_Connect(struct connection *where);
		static void SQL_Close(struct connection *where);
		void create_table_if_needed(void);
		uint64_t SQL_Execute(void);
		uint64_t SQL_Count(void);
		char *SQL_Escape(const char *string, unsigned int max_len);
	private:
		uint64_t _id;
		struct ustat _u;
		struct stime _t;
		char _path[MAX_PATH_SIZE];
		char _query[MAX_Q_SIZE];
		uint64_t _obj_type;
		uint64_t _data_len;
		bool _validated;
		struct connection *_c;
		
		uint64_t atoi(char *str);
		void load(const char *p);
};

void SQL_INIT(void);
void SQL_DESTROY(void);
#endif
