#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <exception>
#include "mempool.h"

//magic number, crc32 of "MemPool"
#define MEMPOOL_MAGIC 0xafcc3f76


typedef struct{
    uint32_t magic;
    void* next;
}MemPoolPt;


MemPool::MemPool( bool multi_thread, size_t item_size, int pre_alloc, int max_pool )
{
    int i;
    MemPoolPt* pt;
    char* p;
    
    multi_thread_ = multi_thread;
    item_size_    = (int)item_size;
    pool_         = NULL;
    count_        = 0;
    max_pool_ = max_pool;
    
    for( i = 0; i < pre_alloc; i++ )
    {
        p = (char*)malloc( item_size_ + sizeof(MemPoolPt) );
        if( !p )
            throw std::bad_alloc();
            
        pt = (MemPoolPt*)(p+item_size_);
        pt->magic = MEMPOOL_MAGIC;
        pt->next  = pool_;
        pool_ = p;
        
        count_ ++;
        
    }
}

MemPool::~MemPool()
{
    MemPoolPt *pt;
    void *p, *next;
    
    p = pool_;
    
    while( p )
    {
        pt = (MemPoolPt*)((char*)p + item_size_);
        next = pt->next;
        
        free( p );
        
        p = next;
    }
    
    
}

void* MemPool::Alloc()
{
    MemPoolPt *pt;
    char* p;
    
    lock();
    
    if( pool_ )
    {
        p = (char*)pool_;
        
        pt = (MemPoolPt*)(p + item_size_);
        
        pool_ = pt->next;
        count_ --;
    }
    else
    {
        p = (char*)malloc( sizeof(MemPoolPt) + item_size_ );
        if( !p )
        {
            unlock();
            return p;
        }
        
        pt = (MemPoolPt*)(p+item_size_);
        pt->magic = MEMPOOL_MAGIC;
    }
    
    unlock();
    
    return p;
    
    
}


void  MemPool::Release( void* data )
{
    MemPoolPt *pt;
    char* p;
        
    if( !data )
        return;
    
    
    p  = (char*)data;
    pt = (MemPoolPt*)(p+item_size_);
    if( pt->magic != MEMPOOL_MAGIC )
    {
        write_log_( "[ERR] memory block corrupt! addr: %p", data );
        return;
    }
    
    lock();
    
    if( max_pool_ && count_ >= max_pool_ )
    {
        free( data );
        unlock();
        return;
    }
    
    pt->next = pool_;
    count_++;
    pool_ = data;
    
    unlock();
    
    return;
    
    
}


inline void MemPool::lock()
{
    if( !multi_thread_ )
        return;
        
    lock_.lock();
}

inline void MemPool::unlock()
{
    if( !multi_thread_ )
        return;
        
    lock_.unlock();
}


int MemPool::Count()
{
    return count_;
}

int MemPool::ItemSize()
{
    return item_size_;
}

bool MemPool::MultiThread()
{
    return multi_thread_;
}

int MemPool::write_log_( const char* fmt, ... )
{

#define LOG_HIGH_RESOLUTION_TIME 1

    FILE* f;
    struct tm* tm;
    va_list  ap;
    time_t t;
    
#ifdef LOG_HIGH_RESOLUTION_TIME
#ifdef _WIN32
    struct timespec tms;
#else
    struct timeval tmv;
#endif
#endif

    f = fopen( "mempool_error.log", "a" );
    if( f == NULL )
        return -1;

#ifdef LOG_HIGH_RESOLUTION_TIME

#ifdef _WIN32
    timespec_get(&tms, TIME_UTC);
    t = tms.tv_sec;
#else
    gettimeofday( &tmv, NULL );
    t = tmv.tv_sec;
#endif
    
#else
    t = time( 0 );
#endif
    
    tm = localtime( &t );
    

    va_start( ap, fmt );
    
#ifdef LOG_HIGH_RESOLUTION_TIME

#ifdef _WIN32
    fprintf( f, "[%02d-%02d %02d:%02d:%02d.%06d]  ",tm->tm_mon+1,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,(int)(tms.tv_nsec/1000) );
#else
    fprintf( f, "[%02d-%02d %02d:%02d:%02d.%06d]  ",tm->tm_mon+1,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,(int)tmv.tv_usec );
#endif

#else
    fprintf( f, "[%02d-%02d %02d:%02d:%02d]  ", tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec );
    //fprintf( f, "[%d-%02d-%02d %02d:%02d:%02d]  ", tm->tm_year + 1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec );
#endif
    
    vfprintf( f, fmt, ap);
    fprintf( f, "\n" );
    va_end( ap );
    
    fclose( f );
    return 0;
}

