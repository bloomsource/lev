#ifndef _LEV_CON_H_
#define _LEV_CON_H_
#include "lev.h"
#include "mempool.h"
#include "dynbuf.h"
#ifdef LEV_CON_SSL
#include <openssl/ssl.h>
#endif
/***********************************************************

lev_con - a tcp/ssl connection class based on lev event loop
developed by wang hai ou

http://www.bloomsource.org

any bug or questions, mail to whotnt@126.com

***********************************************************/

class LevNetEventNotifier
{
    
public:
    
    virtual ~LevNetEventNotifier(){}
    
    //when recv data, if fatal error happend in callback function, set fatal to true
    virtual void OnLevConMsgRecv( const char* msg, size_t len, bool& fatal ){}
    
    //when connection close
    virtual void OnLevConClose( int err ){}
    
    //when async connect failed
    virtual void OnLevConConnectFail(){}
    
    //when async connect ok
    virtual void OnLevConConnectOk(){}
    
    //when SSL_connect ok
    virtual void OnSSLConnectOk(){}
    
    //when SSL_connect fail
    virtual void OnSSLConnectFail( int ssl_errno ){}
    
    //when SSL_accept ok
    virtual void OnSSLAcceptOk(){}
    
    //when SSL_accept failed
    virtual void OnSSLAcceptFail( int ssl_errno ){}
    
    //when SSL_connect/SSL_accept timeout
    //this is usefull to detect if a client is hacking the server
    //( after tcp connection establish, doing nothing )
    virtual void OnSSLInitTimeout(){}

    //when send buffer empty(all data in buffer send to tcp stack, send buffer become empty)
    //this is function is not called by default, 
    //if you want to send this notify,call LevNetConnection::SetNotifySendBufEmpty( true )
    virtual void OnLevConBufferEmpty(){}
    
    //when data is write to tcp stack ok, this is usefull on special senario.
    //len is how many bytes data is send, bufdata is true means data is from send buffer
    //  (not directly write to tcp protocl stack when you call LevNetConnection::SendData() )
    //this function is not called by default, 
    //if you want to send this notify, call LevNetConnection::SetNotifyDataSend( true )
    virtual void OnLevDataSend( int len, bool bufdata ){}
    
};

class LevNetConnection
{
    
public:
    
    //the fd will be automaticly closed when destruct
    LevNetConnection( LevEventLoop* loop, lev_sock_t fd, LevNetEventNotifier* notifier, MemPool* pool = NULL );
    
    virtual ~LevNetConnection();
    
    //get socket fd
    lev_sock_t GetFd();
    
    //set event notifier
    void SetNotifier( LevNetEventNotifier* notifier );
    
    //return the size of data in send buf
    //(data is buffered in send buffer, not send to tcp stack yet)
    virtual int DataInBuffer() = 0;
    
    //set if call OnLevDataSend notify function when data is send
    void SetNotifyDataSend( bool notify );
    
    //set if call OnLevConBufferEmpty notify function when send buffer become empty
    void SetNotifySendBufEmpty( bool notify );
    
    //send data to remote, if failed, you should close connetion
    virtual bool SendData( const void* msg, size_t msglen ) = 0;
    
    //send data to remote, and after message send compelete, 
    //close the connection( call OnLevConClose ).
    virtual bool SendAndClose( const void* msg, size_t msglen ) = 0;
    

protected:

    lev_sock_t fd_;
    
    LevNetEventNotifier* notify_;
    
    LevEventLoop* loop_;
    
    DynamicBuf snd_buf_;
    
    //after message send to remote compelete, 
    //close the connection;
    bool want_close_;
    
    bool notify_send_;
    
    bool notify_buf_empty_;
    
};

class LevTcpConnection : public LevNetConnection
{
    
public:
    
    LevTcpConnection( LevEventLoop* loop, lev_sock_t fd, LevNetEventNotifier* notifier, MemPool* pool = NULL, bool connected = true );
    
    virtual ~LevTcpConnection();
    
    void StopRecv();
    void ContinueRecv();
    
    int DataInBuffer() override;
    
    //send data to remote, if failed, you should close connetion
    bool SendData( const void* msg, size_t msglen ) override;
    
    //send data to remote, and after message send compelete, 
    //close the connection( call OnLevConClose ).
    bool SendAndClose( const void* msg, size_t msglen ) override;
    
private:
    
    friend void LevTcpIoReadCB( LevEventLoop* loop, lev_sock_t fd, void* data );
    friend void LevTcpIoWriteCB( LevEventLoop* loop, lev_sock_t fd, void* data );
    
    void ProcReadEvent();
    void ProcWriteEvent();
    
    //already connected, this flag is used to proc tcp async connect
    bool connected_;
};


#ifdef LEV_CON_SSL

#define LEV_SSL_INIT_TIMEOUT  3

typedef enum{
    SSL_CLIENT,
    SSL_SERVER
}LevSSLConnetionType;

class LevSSLConnection : public LevNetConnection
{
    
public:
    
    //this called after tcp connection established and SSL_new ok, then begin to ssl handshake , after handshake ok, you can perform read/write
	//the fd/ssl will be automaticly closed/free when destruct
    LevSSLConnection( LevEventLoop* loop, SSL* ssl, lev_sock_t fd, LevSSLConnetionType type, LevNetEventNotifier* notifier, MemPool* pool = NULL );
    
    virtual ~LevSSLConnection();
    
    SSL* GetSsl();
    
    int DataInBuffer() override;
    
	//if ssl hankshake not compelte, data will store in buffer, after handshake compelte, data is going to send
    bool SendData( const void* msg, size_t msglen ) override;
    
    bool SendAndClose( const void* msg, size_t msglen ) override;
    
private:
    
    friend void LevSSLIoReadCB( LevEventLoop* loop, lev_sock_t fd, void* data );
    friend void LevSSLIoWriteCB( LevEventLoop* loop, lev_sock_t fd, void* data );
    friend void LevSSLInitTimeoutCB( LevEventLoop* loop, int timer_id,  void* data );
    
    void ProcReadEvent();
    void ProcWriteEvent();
    void ProcInitTimeout();
    
    void ProcSslInit();
    
    void ProcSslIo( int evt );
    
    LevSSLConnetionType type_;
    
    bool init_ok_;

    int data_in_buf_;
    
    int timer_id_;
    
    SSL* ssl_;
    
};

#endif



#endif
