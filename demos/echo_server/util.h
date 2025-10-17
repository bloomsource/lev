#ifndef _UTIL_H_
#define _UTIL_H_
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif
#include <string>
#include <vector>
#include <map>
#include <thread>

#ifndef _SOCKET_TYPE_DEFINE_
#define _SOCKET_TYPE_DEFINE_
#ifdef _WIN32
typedef SOCKET sock_t;
#define ESOCK  (INVALID_SOCKET)
#elif defined( __linux__ )
typedef int    sock_t;
#define ESOCK (-1)
#else
#error unsupported system
#endif
#endif

#define ERR strerror( errno )

extern char log_file[100];

//close socket
void CloseSocket( sock_t fd );

//tcp listen, if ip is NULL or "*", bind on all interface, or bind on ip
//return ESOCK on fail or socket fd on success
sock_t TcpListen( const char* ip, int port );

//tcp accept, return ESOCK on fail or socket fd on success
sock_t TcpAccept( sock_t lsnfd, char* ip, int& port );



#ifdef __GNUC__
#define LOG_FMT_FLAG __attribute__ ((format (printf, 1, 2 )))
#else
#define LOG_FMT_FLAG
#endif

int WriteLog( const char* fmt, ... ) LOG_FMT_FLAG;


char* strbcpy( char* dest, size_t size, const char* src );

#endif
