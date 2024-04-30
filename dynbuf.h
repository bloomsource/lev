#ifndef _DYN_BUF_H_
#define _DYN_BUF_H_
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mutex>
#include "mempool.h"
/***********************************************************
dynamic buffer utility
developed by wang hai ou
http://www.bloomsource.org/

any bug or questions, mail to whotnt@126.com

***********************************************************/


#define DYNBUF_BLOCK_SIZE  1024

typedef struct DynBufBlock{
    int  offset;  //offset of data
    int  len;     //how many data in this block
    struct DynBufBlock* next;
    char buf[DYNBUF_BLOCK_SIZE];
}DynBufBlock;


class DynamicBuf
{
    
public:
    
    /*
    DynamicBuf have a MemPool it self, but you can also specify a MemPool for it,
    so different DynamicBuf can share a same MemPool.
    
    when pool is not null, ItemSize of pool MUST == sizeof(DynBufBlock),
    or will throw std::invalid_argument
    */
    DynamicBuf( bool multi_thread, MemPool* pool = NULL );
    
    
    ~DynamicBuf();
    
    /*
    dynbuf clear function
    */
    void Clear();
    
    /*
    dynbuf read function, read data from head of dynbuf
    
    buf    - buf to store readed data
    size   - size of buf
    actual - size of actual read data
    
    return:
    true   - ok
    false  - no data in ring buffer
    */
    bool Read( char* buf, int size, int& actual );

    /*
    dynbuf peek function
    
    buf    - buf to store readed data
    size   - size of buf
    actual - size of actual peek data
    
    return:
    true   - ok
    false  - no data in ring buffer
    
    note:
    the data is still in the dynbuf after success
    
    how to use:
    you can peek 1k from buffer, after send, only
    500 bytes send ok, then you can call a Inc( 500 )
    after send.
    */
    
    bool Peek( char* buf, int size, int& actual );
    
    /*
    write data to dynbuf
    
    data - data to write to buf
    size - length of data
    
    return:
    true  - ok
    false - failed
    */
    bool Write( const void* data, size_t size );
    
    /*
    dynbuf fetch msg function
    net_byte_order - the msg length identifier is network byte order
    min_size       - min size of msg
    max_size       - max size of msg
    cmd            - cmd code of msg
    msg            - buffer to store msg
    
    
    return:
    negatie   - error happend, you should close connection
    positive  - a msg is successfuly fetch, and stored in msg, 
                return value is length of this message
    0         - message not recv compelete
    
    note:
    the struct must be packed, and first and second element must be int,
    len is total length of struct
    
    #pragma pack(1)
    typedef struct CmdLogin{
        uint32_t len;
        uint32_t cmd;
        char name[16];
        char passwd[16];
    }CmdLogin;
    #pragma pack()
    
    example:
    
    int msglen;
    int cmd;
    char msg[1024];
    
    bool proc_msg( int cmd, char* msg, int msglen );
    
    while( ( msglen = recvbuf.FetchMsg32( true, sizeof(int)*2, 1024, cmd, msg ) ) > 0 )
    {
        if( !proc_msg( cmd, msg, msglen ) )
        {
            delete this;
            return;
        }
    }
    
    if( msglen < 0 )
    {
        write_log( "[ERR] client recv invalid msg!" );
        delete this;
        return;
    
    }
    
    */
    int FetchMsg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg );

    /*
    dynbuf peek msg function
    
    note:
    this function is just like FetchMsg, but after call, msg is stll in the buffer,
    PeekMsg is usefull when you need to perform a operate that can't compelete one time
    and need to recall like nonblocking SSL_Write
    */
    int PeekMsg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg );
    
    
    /*fetch/peek 16 bits version msg
    dynbuf fetch msg function
    net_byte_order - the msg length identifier is network byte order
    min_size       - min size of msg
    max_size       - max size of msg
    cmd            - cmd code of msg
    msg            - buffer to store msg
    
    
    return:
    negatie   - error happend, you should close connection
    positive  - a msg is successfuly fetch, and stored in msg, 
                return value is length of this message
    0         - message not recv compelete
    
    note:
    the struct must be packed, and first and second element must be short,
    len is total length of struct
    
    #pragma pack(1)
    typedef struct CmdLogin{
        uint16_t len;
        uint16_t cmd;
        char name[16];
        char passwd[16];
    }CmdLogin;
    #pragma pack()
    
    */
    int FetchMsg16( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg );
    int PeekMsg16 ( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg );
    
    /*
    dynbuf incr function
    incr   - how many bytes your read pointer to increment
    
    return:
    true   - ok
    false  - fail
    
    note:
    this function increment read pointer of ring buffer
    */
    bool Inc( int incr );
    
    /* how many data in the DynmaicBuf */
    int Len();

    /* if the dynbuf is multi thread */
    bool MultiThread();
    
private:
    
    int read_msg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg, bool fetch );
    int read_msg16( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg, bool fetch );
    
    inline void lock();
    
    inline void unlock();
    
    
    void read( char* buf, int size );
    
    void peek( char* buf, int size );
    
    DynBufBlock* alloc_list( int cnt );
    
    void free_list( DynBufBlock* blk );
    
    void write_from_block( DynBufBlock* blk, const char* buf, int size, DynBufBlock* &end );
    
    //how many free space in this block
    int block_space( DynBufBlock* blk );
    
    int free_space();
    
    uint32_t htonl_( uint32_t val );
    uint32_t ntohl_( uint32_t val );
    
    uint16_t htons_( uint16_t val );
    uint16_t ntohs_( uint16_t val );
    
    void detect_byte_order();
    
    
private:

    
    std::mutex lock_;
    
    int byte_order_; //byte order
    int len_;     //how many data in buffer
    int blk_cnt_; //how many storeage block in buffer
    
    DynBufBlock* head_;
    DynBufBlock* tail_;
    
    bool multi_thread_;
    
    MemPool* pool_;
    MemPool  pool_self_;
    
    
    
};


#endif
