#ifndef _LEV_HEADER_H_
#define _LEV_HEADER_H_
#ifdef _WIN32
#include <winsock2.h>
#endif
/***********************************************************

lev - low latency event loop library
developed by wang hai ou

http://www.bloomsource.org

any bug or questions, mail to whotnt@126.com

***********************************************************/


#ifdef _WIN32
typedef SOCKET lev_sock_t;
#else
typedef int    lev_sock_t;
#endif

#define LEV_INVALID_TIMER_ID          (-1)

#define LEV_IO_EVENT_READ  1
#define LEV_IO_EVENT_WRITE 2

class LevEventLoop;

//socket fd event callback function
typedef void (*LevIoCallback)( LevEventLoop* loop, lev_sock_t fd, void* data );
//timer callback function
typedef void (*LevTimerCallback)( LevEventLoop* loop, int timer_id,  void* data );
//customer callback function
typedef void (*LevCustFuncCallback)( LevEventLoop* loop, void* data );

class LevEventLoop{
    
public:
    
    virtual ~LevEventLoop(){}
    
    //event loop run, this function will block untill event loop stop
    virtual void Run() = 0;
    
    //event loop stop, you can call this function on signal callback function
    virtual void Stop() = 0;
    
    //set sleep time, this is max time (miliseoncds) system call will block
    //this will only work on high latency mode
    virtual void SetSleepTime( int miliseoncds ) = 0;
    
    //add a socket fd to watch list, return true when success, return false when fail
    //note:
    //1.event should be LEV_IO_EVENT_READ or LEV_IO_EVENT_WRITE
    //2.duplicate call of this function with same parameters will fail
    virtual bool AddIoWatcher( lev_sock_t fd, int event, LevIoCallback cb, void* data ) = 0;
    
    //delete a sock fd from watch list
    //note:
    //1.event should be LEV_IO_EVENT_READ or LEV_IO_EVENT_WRITE
    //2.if a socket fd is watch both read and write event, 
    //after you delete read event , write event still in watch.
    virtual bool DeleteIoWatcher( lev_sock_t fd, int event ) = 0;
    
    //delete a sock fd from watch list, both read and read event.
    virtual bool DeleteIoWatcher( lev_sock_t fd ) = 0;
    
    //add a timer event, this function return true when success, return false when fail
    //start is how many seconds this timer will be called, 
    //interval is timer interval, 
    //if interval is 0, this timer only call once, and auto deleted after callback.
    //id is timer id, when success, id will be set as timer id, when fail, id will be set as LEV_INVALID_TIMER_ID
    virtual bool AddTimerWatcher( double start, double interval, LevTimerCallback cb, void* data, int& id ) = 0;
    
    //delete a timer
    virtual bool DeleteTimerWatcher( int id ) = 0;
    
    //add a customer call back function, function will called in main loop
    //the callback function MUST BE NONBLOCKING
    virtual bool AddCustFunc( LevCustFuncCallback cb, void* data ) = 0;
    
    //delete a customer call back function
    virtual bool DeleteCustFunc( LevCustFuncCallback cb, void* data ) = 0;
    
    //add a new task, task function will only execute once and auto deleted
    virtual bool AddCustTask( LevCustFuncCallback cb, void* data ) = 0;
    
    //delete socket fd from watch list, and close the socket.
    //note:
    //this function equal to first call DeleteIoWatcher( fd ) , then call close(fd)/closesocket(fd)
    virtual void Close( lev_sock_t fd ) = 0;
    
    //delete all sockets in the watch list and close them
    virtual void CloseAll() = 0;
    
};

//init lev envrionment, on win32 platform, it do winsock init.
bool LevInitEnvironment();

//create a event loop object, return NULL when failed, return none NULL when success
//when low_latency is true, this is low latency mode. 
//     note: low_latency mode will consume cup 100%, only program need extremely low latency
//     should use low_latency( like high frequency trading )
//when low_latency is false, this is high latency mode, normal program should use this mode.
//call delete to free this object
LevEventLoop* LevCreateEventLoop( bool low_latency );

//get the default event loop object, return NULL when failed, return none NULL when success
//you should not call delete on the default eventloop, it can free automaticly.
//the returned event loop object is high latency mode
LevEventLoop* LevGetDefaultLoop();

//stop all the event loop
void LevStopAllEventLoop();

//this is a helper function, it set a socket fd to nonblocking mode.
//you should set socket to nonblocking mode if you use lev
bool LevSetNonblocking( lev_sock_t fd );



#endif

