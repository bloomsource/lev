#include "server.h"
#include "client.h"

void SigProc( int sig )
{
    LevStopAllEventLoop();
}

void NewClientCB( LevEventLoop* loop, lev_sock_t fd, void* data )
{
    Server* srv = (Server*)data;
    srv->ProcNewClient();
}

Server::Server()
: pool_( false, sizeof(DynBufBlock) )
{
    strcpy( log_file, "echo_server.log" );
    
    lsn_fd_ = ESOCK;
    cli_id_ = 1;
    
    LevInitEnvironment();
    
}

Server::~Server()
{
    Client* cli;
    if( lsn_fd_ != ESOCK )
        loop_->Close( lsn_fd_ );
    
    while( clients_.size() )
    {
        auto it = clients_.begin();
        
        cli = it->second;
        
        delete cli;
        
    }
    
    
    WriteLog( "[WRN] echo_server stop." );
    
    
}

bool Server::Init( int port )
{
    loop_ = LevGetDefaultLoop();
    
    if( !loop_ )
    {
        printf( "GetEventLoop failed!\n" );
        WriteLog( "GetEventLoop faled!" );
        
        return false;
    }
    
    port_ = port;
    
    lsn_fd_ = TcpListen( NULL, port );
    if( lsn_fd_ == ESOCK )
    {
        printf( "TcpListen failed!\n" );
        WriteLog( "TcpListen failed! err: %s", ERR );
        return false;
    }
    
    loop_->AddIoWatcher( lsn_fd_, LEV_IO_EVENT_READ, NewClientCB, this );
    
    signal( SIGINT,  SigProc );
    signal( SIGTERM, SigProc );
    signal( SIGPIPE, SIG_IGN );
    
    return true;
}

void Server::Run()
{
    loop_->Run();
    
}

void Server::ProcNewClient()
{
    sock_t fd;
    char ip[16];
    int port;
    int id;
    Client* cli;
    
    
    fd = TcpAccept( lsn_fd_, ip, port );
    if( fd == ESOCK )
    {
        printf( "TcpAccept failed!\n" );
        WriteLog( "[ERR] TcpAccept failed! err: %s", ERR );
        return;
    }
    
    id = cli_id_++;
    
    WriteLog( "new client %s:%d id: %d", ip, port, id );
    
    cli = new Client( id, loop_, this, &pool_, fd, ip, port );
    
    clients_[id] = cli;
    
}


void Server::DeletClientID( int id )
{
    clients_.erase( id );
}









