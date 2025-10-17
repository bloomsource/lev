#ifndef _CLIENT_H_
#define _CLIENT_H_
#include "util.h"
#include "lev_con.h"

class Server;

class Client : public LevNetEventNotifier
{
public:

    
    Client( int id, LevEventLoop* loop, Server* srv, MemPool* pool, sock_t fd, char* ip, int port );
    
    ~Client();

    void Free();
    

private:

    void OnLevConMsgRecv( const char* msg, size_t len, bool& fatal ) override;
    
    void OnLevConClose( int err ) override;
    
    bool free_flag_ = false;
    
    int id_;
    
    char ip_[16];
    int  port_;
    
    LevTcpConnection con_;
    Server* srv_;
    
    LevEventLoop* loop_;
    
};





#endif

