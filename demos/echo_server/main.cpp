#include "util.h"
#include "server.h"

void usage()
{
    printf( "usage:echo_server port\n" );
}

int main( int argc, char* argv[] )
{
    Server srv;
    
    if( argc == 1 )
    {
        usage();
        return 1;
    }
    
    if( !srv.Init( atoi( argv[1] ) ) )
    {
        printf( "echo_server init failed! exit.\n" );
        WriteLog( "echo_server init failed!" );
        return 1;
    }
    
    printf( "echo_server start ok!\n" );
    
    srv.Run();
    
    
    return 0;
}

