#ifndef ENTRY_H
#define ENTRY_H

struct entry {
	char *data;
	uint64_t len;
	uint64_t id;
	uint64_t uid;
	uint64_t gid;
	uint64_t mode;
	uint64_t obj_type;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint64_t btime;
	char *path;
	struct entry *next;
};

struct entry_list {
	struct entry *head;
	struct entry *tail;
	signed long long count;
};
#define OBJ_ANY 	0
#define OBJ_FILE 	1
#define OBJ_DIR 	2
#define OBJ_LINK 	3

#define EMPTY_DATA	1
#define FILL_DATA	2
#define SAVE_DATA	1
#define SAVE_STAT	2
#define SAVE_TIME	3
#define SAVE_ALL	4
#define EXISTS(path) 	(is_type(path,OBJ_ANY))


extern char *identifier;
struct entry *entry_create(const char *path,const char *extra);
int entry_delete(struct entry *e);
int entry_save(struct entry *e,int what);
int entry_rename(struct entry *e, const char *dest);
struct entry *entry_get(const char *path,size_t size, off_t offset,size_t min_size,int fill);
void entry_destroy(struct entry *d);
void create_root_if_needed(void);
int is_type(const char *path, int type);
void entry_list_init(struct entry_list *el);
void entry_list_destroy(struct entry_list *el);
void entry_list_fill(struct entry_list *el,char *query, int fill);
char *sql_sanitize(const char *s);
uint64_t sql_count(char *query);
uint64_t sql_do(char *query);
void sql_connect(void);
void sql_close(void);
#endif