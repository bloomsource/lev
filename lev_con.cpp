#include "lev_con.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

static bool tcp_async_connect_ok( lev_sock_t fd );
static bool tcp_err_nonblocking( int ret );

LevNetConnection::LevNetConnection( LevEventLoop* loop, lev_sock_t fd, LevNetEventNotifier* notifier, MemPool* pool )
: snd_buf_( false, pool )
{
    loop_ = loop;

    fd_   = fd;

    notify_ = notifier;

    want_close_ = false;

    notify_send_ = false;
    
    notify_buf_empty_ = false;
    
    LevSetNonblocking( fd );
    
}
    
LevNetConnection::~LevNetConnection()
{
    loop_->Close( fd_ );
}

lev_sock_t LevNetConnection::GetFd()
{
    return fd_;
}

void LevNetConnection::SetNotifier( LevNetEventNotifier* notifier )
{
    if( notifier )
        notify_ = notifier;
}

void LevNetConnection::Stop()
{
    loop_->DeleteIoWatcher( fd_ );
}

void LevNetConnection::SetNotifyDataSend( bool notify )
{
    notify_send_ = notify;
}

void LevNetConnection::SetNotifySendBufEmpty( bool notify )
{
    notify_buf_empty_ = notify;
}

void LevTcpIoReadCB( LevEventLoop* loop, lev_sock_t fd, void* data )
{
    LevTcpConnection* con;
    con = (LevTcpConnection*)data;

    con->ProcReadEvent();

}

void LevTcpIoWriteCB( LevEventLoop* loop, lev_sock_t fd, void* data )
{
    LevTcpConnection* con;
    con = (LevTcpConnection*)data;

    con->ProcWriteEvent();

}

LevTcpConnection::LevTcpConnection( LevEventLoop* loop, lev_sock_t fd, LevNetEventNotifier* notifier, MemPool* pool, bool connected )
: LevNetConnection( loop, fd, notifier, pool )
{

    connected_  = connected;

    if( connected_ )
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_READ, LevTcpIoReadCB, this );
    else
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevTcpIoWriteCB, this );


}


LevTcpConnection::~LevTcpConnection()
{
}

void LevTcpConnection::ProcReadEvent()
{
    char buf[1024];
    int rc;
    bool fatal;

    rc = recv( fd_, buf, sizeof(buf), 0 );
    if( rc <= 0 )
    {
        Stop();
        notify_->OnLevConClose( 0 );
        return ;
    }

    fatal = false;
    if( !want_close_)
    {
        notify_->OnLevConMsgRecv( buf, rc, fatal );
        if( fatal )
            Stop();
        
    }
    
}

void LevTcpConnection::ProcWriteEvent()
{
    char buf[1024];
    int msglen, rc;

    if( !connected_ )
    {
        if( !tcp_async_connect_ok( fd_ ) ) //async connect to remote failed
        {
            notify_->OnLevConConnectFail();
            return;
        }
        
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_READ, LevTcpIoReadCB, this );
        
        if( snd_buf_.Len() == 0  )
            loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );
        
        notify_->OnLevConConnectOk();
        
        connected_ = true;
    }
    else
    {
        if( snd_buf_.Len() == 0 )
        {
            loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );
            return;
        }

        snd_buf_.Peek( buf, sizeof(buf), msglen );

        rc = send( fd_, buf, msglen, 0 );
        if( rc <= 0 )
        {
            Stop();
            notify_->OnLevConClose( 0 );
            return ;
        }
        
        snd_buf_.Inc( rc );

        if( notify_send_ )
            notify_->OnLevDataSend( rc, true );
        
        if( snd_buf_.Len() ) //some data still in buffer
            return;

        loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );

        if( notify_buf_empty_ )
            notify_->OnLevConBufferEmpty();
        

        if( want_close_ )
        {
            Stop();
            notify_->OnLevConClose( 0 );
            return ;
        }
    }

}

int LevTcpConnection::DataInBuffer()
{
    return snd_buf_.Len();
}

void LevTcpConnection::StopRecv()
{
    loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_READ );
}

void LevTcpConnection::ContinueRecv()
{
    loop_->AddIoWatcher( fd_, LEV_IO_EVENT_READ, LevTcpIoReadCB, this );
}

bool LevTcpConnection::SendData( const void* msg, size_t msglen )
{
    int rc;

    if( want_close_ )
        return true;
    
    if( !connected_ )
    {
        return snd_buf_.Write( msg, msglen );
    }
    else
    {
        if( snd_buf_.Len() == 0 )
        {
#ifdef _WIN32
            rc = send( fd_, (char*)msg, (int)msglen, 0 );
#else
            rc = send( fd_, msg, msglen, 0 );
#endif

            if( rc <= 0 )
            {
                if( tcp_err_nonblocking( rc ) )
                {
                    if( !snd_buf_.Write( msg, msglen ) )
                        return false;
                    loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevTcpIoWriteCB, this );
                    return true;
                }
                else
                    return false;
            }
            
            if( notify_send_ )
                notify_->OnLevDataSend( rc, false );
            
            //some data not send compelete
            if( rc != (int)msglen )
            {
                if( !snd_buf_.Write( (char*)msg+ rc, msglen - rc ) )
                    return false;
                loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevTcpIoWriteCB, this );
            }
        }
        else
        {
            if( !snd_buf_.Write( msg, msglen ) )
                return false;
        }
        return true;
    }
}

bool LevTcpConnection::SendAndClose( const void* msg, size_t msglen )
{
    if( want_close_ )
        return true;
    
    if( !snd_buf_.Write( msg, msglen ) )
        return false;
    
    want_close_ = true;

    loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevTcpIoWriteCB, this );
    
    return true;
    
}

bool tcp_async_connect_ok( lev_sock_t fd )
{
    int opt;
#ifdef _WIN32
    int len;
#else
    socklen_t len;
#endif

    len = sizeof(opt);
    getsockopt( fd, SOL_SOCKET, SO_ERROR, (char*)&opt, &len );
    if( opt ) /* connect to remote server failed */
        return false;
    
    return true;
}

bool tcp_err_nonblocking( int ret )
{
#ifdef _WIN32
    if( ret == SOCKET_ERROR && ( WSAGetLastError () ==  WSAEWOULDBLOCK ) )
        return true;
#else
    if( ret == -1 && errno == EWOULDBLOCK )
        return true; 
#endif
    return false;
}

#ifdef LEV_CON_SSL

#define LEV_SSL_BLOCK_SIZE    1024

#define LEV_SSL_EVENT_READ  1
#define LEV_SSL_EVENT_WRITE 2

void LevSSLIoReadCB( LevEventLoop* loop, lev_sock_t fd, void* data )
{
    LevSSLConnection* con;
    con = (LevSSLConnection*)data;

    con->ProcReadEvent();

}

void LevSSLIoWriteCB( LevEventLoop* loop, lev_sock_t fd, void* data )
{
    LevSSLConnection* con;
    con = (LevSSLConnection*)data;

    con->ProcWriteEvent();
    
}

void LevSSLInitTimeoutCB( LevEventLoop* loop, int timer_id,  void* data )
{
    LevSSLConnection* con;
    con = (LevSSLConnection*)data;

    con->ProcInitTimeout();
}

LevSSLConnection::LevSSLConnection( LevEventLoop* loop, SSL* ssl, lev_sock_t fd,  LevSSLConnetionType type, LevNetEventNotifier* notifier, MemPool* pool )
: LevNetConnection( loop, fd, notifier, pool )
{

    ssl_ = ssl;
    type_ = type;
    init_ok_  = false;
    timer_id_ = LEV_INVALID_TIMER_ID;
    SSL_set_fd( ssl, (int)fd );
    data_in_buf_ = 0;
    
    
    loop_->AddIoWatcher( fd_, LEV_IO_EVENT_READ, LevSSLIoReadCB, this );
    
    loop_->AddTimerWatcher( LEV_SSL_INIT_TIMEOUT, 0, LevSSLInitTimeoutCB, this, timer_id_ );

    if( type == SSL_CLIENT )
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevSSLIoWriteCB, this );

}

LevSSLConnection::~LevSSLConnection()
{

    if( init_ok_ )
        SSL_shutdown( ssl_ );

    SSL_free( ssl_ );
    
    loop_->DeleteTimerWatcher( timer_id_ );

}

SSL* LevSSLConnection::GetSsl()
{
    return ssl_;
}

void LevSSLConnection::ProcReadEvent()
{

    if( !init_ok_ )
        ProcSslInit();
    else
        ProcSslIo( LEV_SSL_EVENT_READ );

}

void LevSSLConnection::ProcWriteEvent()
{
    
    if( !init_ok_ )
        ProcSslInit();
    else
        ProcSslIo( LEV_SSL_EVENT_WRITE );
}

void LevSSLConnection::ProcInitTimeout()
{

    timer_id_ = LEV_INVALID_TIMER_ID;
    Stop();
    notify_->OnSSLInitTimeout();

}

void LevSSLConnection::ProcSslInit()
{
    int rc;
    int err;

    if( type_ == SSL_CLIENT )
        rc = SSL_connect( ssl_ );
    else
        rc = SSL_accept( ssl_ );

    if( rc > 0 )
    {
        init_ok_ = true;
        
        loop_->DeleteTimerWatcher( timer_id_ );
        timer_id_ = LEV_INVALID_TIMER_ID;

        if( snd_buf_.Len() )
            loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevSSLIoWriteCB, this );
        else
            loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );
        
        
        if( type_ == SSL_CLIENT )
            notify_->OnSSLConnectOk();
        else
            notify_->OnSSLAcceptOk();
    }
    else
    {
        err = SSL_get_error( ssl_, rc );
        if( err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE )
        {
            
            loop_->DeleteTimerWatcher( timer_id_ );
            timer_id_ = LEV_INVALID_TIMER_ID;

            Stop();
            
            if( type_ == SSL_CLIENT )
                notify_->OnSSLConnectFail( err );
            else
                notify_->OnSSLAcceptFail( err );
            return;
        }

        if( err == SSL_ERROR_WANT_READ )
            loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );

        if( err == SSL_ERROR_WANT_WRITE )
            loop_->AddIoWatcher( fd_, LEV_IO_EVENT_READ, LevSSLIoWriteCB, this );
    }
}

void LevSSLConnection::ProcSslIo( int evt )
{
    int rc;
    int err;
    int len;
    int msglen;
    bool fatal, want_write, want_read;
    bool try_write;
    char buf[LEV_SSL_BLOCK_SIZE+sizeof(int)];
    
    want_read  = false;
    want_write = false;
    try_write  = false;

    //if some data in send buffer
    while( snd_buf_.Len() )
    {
        try_write = true;
        snd_buf_.Peek( (char*)&msglen, sizeof(int), len );
        snd_buf_.Peek( buf, msglen, msglen );
        msglen -= sizeof(int);
        
        rc = SSL_write( ssl_, buf+sizeof(int), msglen );
        if( rc > 0 )
        {
            snd_buf_.Inc( msglen + sizeof(int));
            
            data_in_buf_ -= msglen;
            
            if( notify_send_ )
                notify_->OnLevDataSend( rc, true );
        }
        else
        {
            err = SSL_get_error( ssl_, rc );
            if( err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE )
            {
                Stop();
                notify_->OnLevConClose( err );
                return;
            }
            
            if( err == SSL_ERROR_WANT_READ )
            {
                want_read = true;
                break;
            }
                
            
            if( err == SSL_ERROR_WANT_WRITE )
            {
                want_write = true;
                break;
            }
        }
    }

    if( try_write && snd_buf_.Len() == 0 && notify_buf_empty_ )
    {
        notify_->OnLevConBufferEmpty();
    }
    
        

    if( want_close_ && ( snd_buf_.Len() == 0 ) )
    {
        Stop();
        notify_->OnLevConClose( 0 );
        return;
    }
    

    //read event, or last SSL_read errcode is SSL_ERROR_WANT_WRITE
    if( ( evt == LEV_SSL_EVENT_READ ) || (  evt == LEV_SSL_EVENT_WRITE && !try_write  ) )
    {
    
        rc = SSL_read( ssl_, buf, LEV_SSL_BLOCK_SIZE );
        if( rc > 0 )
        {
            if( !want_close_ )
            {
                fatal = false;
                notify_->OnLevConMsgRecv( buf, rc, fatal );

                //fatal is useed to detect if some thing fatal happened in call back function.
                //if fatal happend, usually the connection should closed.
                if( fatal )
                {
                    Stop();
                    return;
                }
            }
        }
        else
        {
            err = SSL_get_error( ssl_, rc );
            if( err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE )
            {
                Stop();
                notify_->OnLevConClose( err );
                return;
            }
            
            if( err == SSL_ERROR_WANT_READ )
                want_read = true;
            
            if( err == SSL_ERROR_WANT_WRITE )
                want_write = true;
            
        }
    }

    if( want_read )
        (void)want_read;

    if( want_write || snd_buf_.Len() )
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevSSLIoWriteCB, this );
    else
        loop_->DeleteIoWatcher( fd_, LEV_IO_EVENT_WRITE );
        
}


int LevSSLConnection::DataInBuffer()
{
    return data_in_buf_;
}

bool LevSSLConnection::SendData( const void* msg, size_t msglen )
{
    int left;
    int write_len;
    int len;
    int offset;
    char buf[LEV_SSL_BLOCK_SIZE+sizeof(int)];
    
    left = (int)msglen;
    offset = 0;

    if( want_close_ )
        return true;
    
    while( left )
    {
        write_len = left < LEV_SSL_BLOCK_SIZE ? left : LEV_SSL_BLOCK_SIZE;
        len = write_len + sizeof(int);
        
        memcpy( buf, &len, sizeof(int) );
        memcpy( buf + sizeof(int), (char*)msg + offset, write_len );
        
        if( !snd_buf_.Write( buf, len ) )
            return false;
        
        data_in_buf_ += write_len;
        
        left   -= write_len;
        offset += write_len;
    }
    
    if( init_ok_ )
        loop_->AddIoWatcher( fd_, LEV_IO_EVENT_WRITE, LevSSLIoWriteCB, this );
    
    return true;
}

bool LevSSLConnection::SendAndClose( const void* msg, size_t msglen )
{
    if( want_close_ )
        return true;
    
    if( !SendData( msg, msglen ) )
        return false;

    want_close_ = true;
    
    return true;
}


#endif
