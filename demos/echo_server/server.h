#include "util.h"
#include "lev_con.h"
#include <map>

class Client;

class Server{
    
public:

    Server();
    
    ~Server();
    
    bool Init( int port );
    
    void DeletClientID( int id );

    void Run();
    

private:
    
    friend void NewClientCB( LevEventLoop* loop, lev_sock_t fd, void* data );
    void ProcNewClient();
    
    int port_;
    
    int cli_id_;
    
    sock_t lsn_fd_;
    
    std::map<int,Client*> clients_;
    
    LevEventLoop* loop_;
    
    MemPool pool_;
    
};
