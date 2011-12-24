#ifndef COMMON_H
#define COMMON_H
#define _MYSQL_USER 		"root"
#define _MYSQL_PASS 		""
#define _MYSQL_DATABASE 	"fs"
#define _MYSQL_HOST 		"localhost"
#define _MYSQL_PORT		0

#define ACTION_CREATE 0
#define ACTION_CHMOD  1
#define ACTION_CHOWN  2
#define ACTION_RENAME 3
#define ACTION_DELETE 4
#define IS_ROOT(path)	(strcmp("/",path) == 0)

#define COPY(a,b,len) bcopy(a,b,len)
#define QUERY(arg...) snprintf(query,sizeof(query),##arg)
#define _E(fmt,arg...) printf("ERR: %s()\t" fmt " [%s:%d]\n",__func__,##arg,__FILE__,__LINE__)

#if DEBUG >= 1
#	define _D(fmt,arg...) printf("%s()\t" fmt " [%s:%d]\n",__func__,##arg,__FILE__,__LINE__)
#else
#	define _D(fmt,arg...)
#endif
#ifndef min
#	define min(a,b) a > b ? b : a
#endif
#ifndef max
#	define max(a,b) a > b ? a : b
#endif

void clean_exit(int rc);
#endif
