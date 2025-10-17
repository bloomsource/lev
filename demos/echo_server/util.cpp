#include "util.h"


#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#endif


char log_file[100];



void CloseSocket( sock_t fd )
{
    
#ifdef _WIN32
    closesocket( fd );
#else
    close( fd );
#endif
    
}


sock_t TcpListen( const char* ip, int port )
{
    //if ip is NULL or ip = "*", bind to all address,
    //if tcp listen failed, return ESOCK

    sock_t fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == ESOCK)
        return ESOCK;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip == NULL || ip[0] == '*')
    {
        addr.sin_addr.s_addr = INADDR_ANY;
        if ( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) )
        {
            CloseSocket(fd);
            return ESOCK;
        }
    }
    else //bind ip  */
    {
        addr.sin_addr.s_addr = inet_addr(ip);
        if ( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) )
        {
            CloseSocket(fd);
            return ESOCK;
        }
    }

    if ( listen(fd, 5) )
    {
        CloseSocket(fd);
        return ESOCK;
    }

    return fd;

}


sock_t TcpAccept( sock_t lsnfd, char* ip, int& port )
{
    struct sockaddr_in addr;
#ifdef _WIN32
    int len;
#else
    socklen_t len;
#endif
    sock_t clifd;

    len = sizeof(addr);
    clifd = accept(lsnfd, (struct sockaddr*) &addr, &len);
    if (clifd == ESOCK )
        return ESOCK;


    len = sizeof(addr);

    port = ntohs(addr.sin_port);
    sprintf(ip, "%s", inet_ntoa(addr.sin_addr));
    return clifd;
}


int WriteLog( const char* fmt, ... )
{

//#define LOG_HIGH_RESOLUTION_TIME 1
//#define LOG_PRINT_YEAR 1
    FILE* f;
    struct tm tm;
    va_list  ap;
    time_t t;

    f = fopen( log_file, "a" );
    if( f == NULL )
        return -1;

#ifdef LOG_HIGH_RESOLUTION_TIME
    struct timespec tms;
#ifdef _WIN32
    timespec_get(&tms, TIME_UTC);
#else
    clock_gettime( CLOCK_REALTIME, &tms );
#endif
    t = tms.tv_sec;
#else //#ifdef LOG_HIGH_RESOLUTION_TIME
    t = time( 0 );
#endif

#ifdef _WIN32
    localtime_s( &tm, &t );
#else    
    localtime_r( &t, &tm );
#endif


    va_start( ap, fmt );
    
#ifdef LOG_HIGH_RESOLUTION_TIME
#ifdef LOG_PRINT_YEAR
    fprintf( f, "[%d-%02d-%02d %02d:%02d:%02d.%06d]  ",tm.tm_year + 1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,(int)(tms.tv_nsec/1000) );
#else
    fprintf( f, "[%02d-%02d %02d:%02d:%02d.%06d]  ",tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,(int)(tms.tv_nsec/1000) );
#endif
#else
#ifdef LOG_PRINT_YEAR
    fprintf( f, "[%d-%02d-%02d %02d:%02d:%02d]  ", tm.tm_year + 1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec );
#else
    fprintf( f, "[%02d-%02d %02d:%02d:%02d]  ", tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec );
#endif
#endif
    
    vfprintf( f, fmt, ap);
    fprintf( f, "\n" );
    va_end( ap );
    
    fclose( f );
    return 0;
}



char* strbcpy( char* dest, size_t size, const char* src )
{
    strncpy( dest, src, size-1 );
    dest[size-1] = 0;
    
    return dest;
}


