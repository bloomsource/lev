#include "dynbuf.h"
#include <exception>

DynamicBuf::DynamicBuf( bool multi_thread, MemPool* pool )
: pool_self_( multi_thread, sizeof(DynBufBlock) )
{
    if( pool )
        pool_ = pool;
    else
        pool_ = &pool_self_;
    
    if( pool_->ItemSize() != sizeof(DynBufBlock) )
        throw std::invalid_argument( "MemPool ItemSize not correct!");
    
    if( pool_->MultiThread() != multi_thread )
        throw std::invalid_argument( "MemPool MultiThread not correct!" );
            
    multi_thread_ = multi_thread;
    
    len_     = 0;
    blk_cnt_ = 0;
    
    head_ = NULL;
    tail_ = NULL;
    
    detect_byte_order();
    
}


DynamicBuf::~DynamicBuf()
{
    if( head_ )
        free_list( head_ );
}
    

void DynamicBuf::Clear()
{
    
    lock();
    
    if( head_ )
        free_list( head_ );
    
    head_ = NULL;
    tail_ = NULL;
    
    len_     = 0;
    blk_cnt_ = 0;
    
    
    unlock();
    
}

bool DynamicBuf::Read( char* buf, int size, int& actual )
{
    lock();
    
    if( len_ == 0 )
    {
        unlock();
        return false;
    }
    
    actual = len_ > size ? size : len_;
    
    read( buf, actual );
    
    unlock();
    
    return true;
    
}


bool DynamicBuf::Peek( char* buf, int size, int& actual )
{
    
    lock();
    
    if( len_ == 0 )
    {
        unlock();
        return false;
    }
    
    actual = len_ > size ? size : len_;
    
    peek( buf, actual );
    
    unlock();
    
    return true;
    
}


bool DynamicBuf::Write( const char* buf, size_t size )
{
    int blk_inc = 0;
    int size_inc;
    int free_size;
    DynBufBlock* blk;
    DynBufBlock* end;
    
    lock();
    
    free_size = free_space();
    
    if( free_size >= (int)size )
    {
        write_from_block( tail_, buf, (int)size, end );
        tail_ = end;
        unlock();
        return true;
    }
    
    size_inc = (int)(size - free_size);
    
    blk_inc = size_inc / DYNBUF_BLOCK_SIZE;
    if( size_inc % DYNBUF_BLOCK_SIZE )
        blk_inc ++;
            
    blk = alloc_list( blk_inc );
    if( !blk )
    {
        unlock();
        return false;
    }
    
    if( tail_ == NULL )
    {
        head_ = blk;
    }
    else
    {
        tail_->next = blk;
        blk = free_size ? tail_ : blk;
    }
    
    write_from_block( blk, buf, (int)size, end );
    
    tail_ = end;
    blk_cnt_ += blk_inc;
    
    unlock();
    
    return true;
    
}

int DynamicBuf::FetchMsg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg )
{
    return read_msg32( net_byte_order, min_size, max_size, cmd, msg, true );
}

int DynamicBuf::PeekMsg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg )
{
    return read_msg32( net_byte_order, min_size, max_size, cmd, msg, false );
}

int DynamicBuf::FetchMsg16( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg )
{
    return read_msg16( net_byte_order, min_size, max_size, cmd, msg, true );
}

int DynamicBuf::PeekMsg16( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg )
{
    return read_msg16( net_byte_order, min_size, max_size, cmd, msg, false );
}

bool DynamicBuf::Inc( int incr )
{
    lock();
    
    if( len_ < incr )
    {
        unlock();
        return false;
    }
    
    read( NULL, incr );
    
    unlock();
    
    return true;
    
}

int DynamicBuf::Len()
{
    int len;
    
    lock();
    
    len = len_;
    
    unlock();
    
    return len;
}

int DynamicBuf::read_msg32( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg, bool fetch )
{
    int msglen;
    int len;
    int code;
    
    lock();
    
    if( len_ < (int)sizeof(int) )
    {
        unlock();
        return 0;
    }
    
    peek( (char*)&msglen, sizeof(int) );
    
    if( net_byte_order )
        msglen = ntohl_( msglen );
    
    len = msglen;
    
    if( len < (int)sizeof(int) * 2 )
    {
        unlock();
        return -1;
    }
    
    if( len < min_size )
    {
        unlock();
        return -1;
    }
    
    if( len > max_size )
    {
        unlock();
        return -1;
    }
    
    if( len_ < len )
    {
        unlock();
        return 0;
    }
    
    if( fetch )
    {
        read( msg, len );
        memcpy( &code, msg+sizeof(int), sizeof(int) );
        if( net_byte_order )
            code = ntohl_( code );
        cmd = code;
    }
    else
    {
        peek( msg,len );
        memcpy( &code, msg+sizeof(int), sizeof(int) );
        if( net_byte_order )
            code = ntohl_( code );
        cmd = code;
    }
    
    unlock();
    
    return len;
}

int DynamicBuf::read_msg16( bool net_byte_order, int min_size, int max_size, int& cmd, char* msg, bool fetch )
{
    uint16_t msglen;
    uint16_t code;
    int len;
    
    
    lock();
    
    if( len_ < (int)sizeof(short) )
    {
        unlock();
        return 0;
    }
    
    peek( (char*)&msglen, sizeof(short) );
    
    if( net_byte_order )
        msglen = ntohs_( msglen );
    
    len = (int)msglen;

    if( len < (int)sizeof(short)*2 )
    {
        unlock();
        return -1;
    }
    
    if( len < min_size )
    {
        unlock();
        return -1;
    }
    
    if( len > max_size )
    {
        unlock();
        return -1;
    }
    
    if( len_ < len )
    {
        unlock();
        return 0;
    }
    
    if( fetch )
    {
        read( msg, len );
        memcpy( &code, msg+sizeof(short), sizeof(short) );
        if( net_byte_order )
            code = ntohs_( code );
        cmd = code;
    }
    else
    {
        peek( msg, len );
        memcpy( &code, msg+sizeof(short), sizeof(short) );
        if( net_byte_order )
            code = ntohl_( code );
        cmd = code;
    }
    
    unlock();
    
    return len;
}

void DynamicBuf::read( char* buf, int size )
{
    int left;
    int wlen;
    int offset;
    DynBufBlock* blk;
    DynBufBlock* next;
    
    left = size;
    blk  = head_;
    offset = 0;
    
    while( left )
    {
        
        wlen = left > blk->len ? blk->len : left;
        
        if( buf )
            memcpy( buf + offset, blk->buf + blk->offset, wlen );
        
        blk->offset += wlen;
        blk->len    -= wlen;
        
        offset += wlen;
        left   -= wlen;
        len_   -= wlen;
        
        next = blk->next;
        
        //no data and no space to write data in this block
        //if( !blk->len && !block_space( blk ) )
        if( blk->offset == DYNBUF_BLOCK_SIZE ) 
        {
            blk_cnt_ --;
            head_ = blk->next;
            pool_->Release( blk );
        }
        
        blk = next;
    }
    
    if( head_ == NULL )
        tail_ = NULL;
    
}

void DynamicBuf::peek( char* buf, int size )
{
    int left;
    int wlen;
    int offset = 0;
    DynBufBlock* blk;
    DynBufBlock* next;
    
    left = size;
    blk  = head_;
    
    while( left )
    {
        wlen = left > blk->len ? blk->len : left;
        
        memcpy( buf + offset, blk->buf + blk->offset, wlen );

        offset += wlen;
        left   -= wlen;
        
        next = blk->next;
        
        blk = next;
    }
}

DynBufBlock* DynamicBuf::alloc_list( int cnt )
{
    int i;
    DynBufBlock* head = NULL;
    DynBufBlock* prev = NULL;
    DynBufBlock* blk;
    
    
    for( i = 0; i < cnt; i++ )
    {
        blk = (DynBufBlock*)pool_->Alloc();
        if( !blk )
        {
            free_list( head );
            return NULL;
        }
        
        blk->offset = 0;
        blk->len    = 0;
        
        if( i == 0 )
            head = blk;

        
        blk->next = NULL;
        
        if( prev )
            prev->next = blk;
        
        prev = blk;
        
    }
    
    return head;
    
}

void DynamicBuf::free_list( DynBufBlock* blk )
{
    DynBufBlock* next;
    
    while( blk )
    {
        next = blk->next;
        
        pool_->Release( blk );
        
        blk = next;
    }
}

void DynamicBuf::write_from_block( DynBufBlock* blk, const char* buf, int size, DynBufBlock* &end )
{
    int left;
    int wlen;
    int blk_space;
    int offset = 0;
    
    left = size;
    
    while( left )
    {
        blk_space = block_space( blk );
        
        wlen = blk_space > left ? left : blk_space;
        
        memcpy( blk->buf + blk->offset + blk->len, buf + offset, wlen );
        
        blk->len += wlen;
        
        len_   += wlen;
        
        left   -= wlen;
        offset += wlen;
        
        if( left == 0 )
            end = blk;
            
        blk = blk->next;
    }
}

//how many free space in this block
int DynamicBuf::block_space( DynBufBlock* blk )
{
    return DYNBUF_BLOCK_SIZE - blk->offset - blk->len;
}

int DynamicBuf::free_space()
{
    if( tail_ )
        return block_space( tail_ );
    else
        return 0;
}

uint32_t DynamicBuf::htonl_( uint32_t val )
{
    
    char c;
    union{ char chars[4]; uint32_t val; } v32;
    
    v32.val = val;
    if( byte_order_ == -1 )
    {
        c = v32.chars[0];
        v32.chars[0] = v32.chars[3];
        v32.chars[3] = c;
        
        c = v32.chars[1];
        v32.chars[1] = v32.chars[2];
        v32.chars[2] = c;
    }

    return v32.val;
}

uint32_t DynamicBuf::ntohl_( uint32_t val )
{
    char c;
    union{ char chars[4]; uint32_t val; } v32;
    
    v32.val = val;
    if( byte_order_ == -1 )
    {
        c = v32.chars[0];
        v32.chars[0] = v32.chars[3];
        v32.chars[3] = c;
        
        c = v32.chars[1];
        v32.chars[1] = v32.chars[2];
        v32.chars[2] = c;
    }

    return v32.val;
}

uint16_t DynamicBuf::htons_( uint16_t val )
{
    char c;
    union{ char chars[2]; uint16_t val; } v16;

    if( byte_order_ == 0 )
        detect_byte_order();
    
    v16.val = val;
    if( byte_order_ == -1 )
    {
        c = v16.chars[0];
        v16.chars[0] = v16.chars[1];
        v16.chars[1] = c;
    }

    return v16.val;
    
}

uint16_t DynamicBuf::ntohs_( uint16_t val )
{
    char c;
    union{ char chars[2]; uint16_t val; } v16;

    if( byte_order_ == 0 )
        detect_byte_order();
    
    v16.val = val;
    if( byte_order_ == -1 )
    {
        c = v16.chars[0];
        v16.chars[0] = v16.chars[1];
        v16.chars[1] = c;
    }

    return v16.val;
    
}


void DynamicBuf::detect_byte_order()
{
    union{ char chars[2]; short val; } v;
    
    v.val = 1;
    if( v.chars[0] == 0 )  //big endian, network order
        byte_order_ = 1;
    else
        byte_order_ = -1;
    
}

bool DynamicBuf::MultiThread()
{
    return multi_thread_;
}

inline void DynamicBuf::lock()
{
    if( !multi_thread_ )
        return;
        
    lock_.lock();
}

inline void DynamicBuf::unlock()
{
    if( !multi_thread_ )
        return;
        
    lock_.unlock();
}



