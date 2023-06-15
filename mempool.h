#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_
#include <stddef.h>
#include <mutex>
/***********************************************************

memory pool utility
developed by wang hai ou
http://www.bloomsource.org/

any bug or questions , mail to whotnt@126.com

***********************************************************/

class MemPool{
    
public:
    
    //if pre_alloc !=0 and memory malloc failed, throw std::bad_alloc
    MemPool( bool multi_thread, size_t item_size, int pre_alloc = 0, int max_pool = 0 );
    
    MemPool() = delete;
    
    ~MemPool();
    
    //return NULL means failed, return none NULL means ok
    void* Alloc();
    
    void  Release( void* data );
    
    int  Count();
    
    int ItemSize();
    
    bool MultiThread();
    
private:
    
    int max_pool_;
    int item_size_;
    int count_;
    
    bool multi_thread_;
    
    
    inline void lock();
    
    inline void unlock();
    
    int write_log_( const char* fmt, ... );
    
    std::mutex lock_;
    
    void* pool_;
    
};





#endif
