#include "client.h"
#include "server.h"

void FreeClient( LevEventLoop* loop, void* data )
{
    Client* cli = (Client*)data;
    
    delete cli;
}


Client::Client( int id, LevEventLoop* loop, Server* srv, MemPool* pool, sock_t fd, char* ip, int port )
: con_( loop, fd, this, pool )
{
    
    id_ = id;
    
    srv_ = srv;
    
    strbcpy( ip_, sizeof(ip_), ip );
    port_ = port;
    
    loop_ = loop;
    
    
}

Client::~Client()
{
    WriteLog( "client %d close.", id_ );
    
    srv_->DeletClientID( id_ );
}


void Client::OnLevConMsgRecv( const char* msg, size_t len, bool& fatal )
{
    con_.SendData( msg, len );
}


void Client::OnLevConClose( int err )
{
    Free();
}

void Client::Free()
{
    if( !free_flag_ )
    {
        loop_->AddCustTask( FreeClient, this );
        free_flag_ = true;
    }
    
}
