#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <atomic>
#include <vector>
#include <list>
#include <map>
#include "lev.h"


#ifdef _WIN32
#include <chrono>
#include <thread>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#endif

#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#endif



//#define LEV_CHECK_VALID_FD  1

#define LEV_EPOLL_EVT_SIZE 10
#define LEV_OBJ_BUF_SIZE   100
#define LEV_CUST_FUNC_SIZE 10
#define LEV_CUST_TASK_SIZE 100

#define LEV_MAX_WAIT_TIME 1000000   //1 second

#define LEV_MIN_TIMER_ID 1
#define LEV_MAX_TIMER_ID 100000000

static bool LevLoopRun  = true;
std::atomic_int  LevInstance(0);

#define find_timer_buf() ( timer_cnt_ <= LEV_OBJ_BUF_SIZE ? timer_buf_ : ext_timer_buf_ )

typedef struct FdCtx{
    int ctx_idx;   //ctx idx in vector
    uint32_t idx;  //fd version idx
}FdCtx;

typedef struct LevIoCtx{
    
    int flag;
    
    lev_sock_t fd;
    uint32_t idx;
    
    int WatchRead;
    LevIoCallback ReadCB;
    void* ReadData;
    
    int WatchWrite;
    LevIoCallback WriteCB;
    void* WriteData;
    
}LevIoCtx;


typedef struct LevTimerCtx{
    
    int      TimerID;
    
    uint64_t TrigTime;
    
    uint64_t Interval;
    
    LevTimerCallback TimerCB;
    
    void* Data;
    
}LevTimerCtx;

typedef struct LevCustFuncCtx{
    
    LevCustFuncCallback cb;
    
    void* data;
    
}LevCustFuncCtx;

static int CompTimer( const void* data1, const void* data2 );

static uint64_t UsecNow();

static void UsecSleep( uint64_t usec );

#ifdef LEV_CHECK_VALID_FD
static bool IsValidFd( lev_sock_t fd );
#endif

#ifdef _WIN32
static void Usec2Tmv( uint64_t usec, struct timeval &tmv );
#endif


class LevEventLoopImpl: public LevEventLoop{
    
public:
    
    LevEventLoopImpl( bool low_latency );
    
    ~LevEventLoopImpl();
    
    bool Init();
    
    void Run() override;
    
    void Stop() override;
    
    void SetSleepTime( int miliseoncds ) override;
    
    bool AddIoWatcher( lev_sock_t fd, int event, LevIoCallback cb, void* data ) override;
    
    bool DeleteIoWatcher( lev_sock_t fd, int event ) override;
    
    bool DeleteIoWatcher( lev_sock_t fd ) override;
    
    bool AddTimerWatcher( double start, double interval, LevTimerCallback cb, void* data, int& id ) override;
    
    bool DeleteTimerWatcher( int id ) override;
    
    bool AddCustFunc( LevCustFuncCallback cb, void* data ) override;
    
    bool DeleteCustFunc( LevCustFuncCallback cb, void* data ) override;
    
    bool AddCustTask( LevCustFuncCallback cb, void* data ) override;
    
    void Close( lev_sock_t fd ) override;
    
    void CloseAll() override;
    
private:
    
    void LoadFds( int start_idx, int end_idx, FdCtx fd_list[], LevIoCtx ctx_list[] );
    
    int  FindFreePos();
    
    bool AddNewTimer( LevTimerCtx ctx);
    
    void ProcFdEvents();
    
    void ProcCustFunc();
    
    void ProcCustTask();
    
    void ProcTimerEvents();
    
    void DeleteTimer( int id );
    
    uint64_t CalcSleepTime( bool poll );
    
    LevTimerCtx* FindTimer( int id );
        
    int NewTimerID();
    
    int timer_id_;
    
    uint32_t fd_idx_;
    
    bool low_latency_;
    
    bool run_;
    
    int epoll_fd_;
    
    int timer_cnt_;
    int timer_ext_buf_size_;
    uint64_t timer_trig_time_;
    
    int sleep_time_;
    
    int cust_func_cnt_;
    
    LevTimerCtx* ext_timer_buf_;
    
    std::map<lev_sock_t, int> fd_map_; //map of fd/ctx_idx(ctx_vec_)
    
    std::vector<LevIoCtx> ctx_vec_;
    
    std::list<LevCustFuncCtx> cust_task_;
    
    LevTimerCtx  timer_buf_[LEV_OBJ_BUF_SIZE];
    
    LevCustFuncCtx cust_func_[LEV_CUST_FUNC_SIZE];
    
};

LevEventLoopImpl::LevEventLoopImpl( bool low_latency )
{
    low_latency_ = low_latency;
    
    if( low_latency )
        sleep_time_ = 0;
    else
        sleep_time_ = LEV_MAX_WAIT_TIME;
    
    run_ = true;
    
    timer_id_ = LEV_MIN_TIMER_ID;
    
    fd_idx_ = 0;
    
    epoll_fd_ = -1;
    
    
    timer_cnt_          = 0;
    timer_ext_buf_size_ = 0;
    timer_trig_time_    = 0;
    ext_timer_buf_      = NULL;
    
    cust_func_cnt_ = 0;
    
}

LevEventLoopImpl::~LevEventLoopImpl()
{
    
    if( ext_timer_buf_ )
        free( ext_timer_buf_ );
    
#ifdef __linux__

    if( epoll_fd_ != -1 )
    {
        close( epoll_fd_ );
    }

#endif
    
}

void LevEventLoopImpl::LoadFds( int start_idx, int end_idx, FdCtx fd_list[], LevIoCtx ctx_list[] )
{
    int idx = 0;
    int cnt = 0;
    
    for( auto it = fd_map_.begin(); it != fd_map_.end(); it++ )
    {
        if( idx >= start_idx && idx <= end_idx )
        {
            fd_list[cnt].ctx_idx = it->second;
            ctx_list[cnt] = ctx_vec_[it->second];
            
            fd_list[cnt].idx = ctx_list[cnt].idx;
            
            cnt++;
        }
        
        idx++;
    }
    
}

int LevEventLoopImpl::FindFreePos()
{
    int i;
    
    for( i = 0; i < (int)ctx_vec_.size(); i++ )
    {
        LevIoCtx& ctx = ctx_vec_[i];
        if( ctx.flag == 0 )
            return i;
    }
    
    return -1;
}

void LevEventLoopImpl::SetSleepTime( int miliseconds )
{
    if( miliseconds <= 0 || miliseconds > LEV_MAX_WAIT_TIME/1000 )
        return ;
    
    sleep_time_ = miliseconds * 1000;
    
}

bool LevEventLoopImpl::Init()
{
    
#ifdef __linux__
    
    epoll_fd_ = epoll_create( 100 );
    if( epoll_fd_ == -1 )
    {
        //write_log( "[ERR] create epoll failed! err:%s\n", strerror( errno ) );
        return false;
    }

#endif
    
    return true;
}

LevTimerCtx* LevEventLoopImpl::FindTimer( int id )
{
    int i;
    LevTimerCtx *pt;
    
    if( timer_cnt_ == 0 )
        return NULL;
    
    pt = find_timer_buf();
    
    for( i = 0; i < timer_cnt_; i++ )
    {
        if( pt[i].TimerID == id )
            return pt+i;
    }
    
    return NULL;
}

uint64_t LevEventLoopImpl::CalcSleepTime( bool poll )
{
    int64_t  diff;
    uint64_t now,sleep_time;
    
    if( low_latency_ )
    {
        sleep_time = 0;
    }
    else
    {
        now = UsecNow();
        if( timer_trig_time_ )
        {
            diff = timer_trig_time_ - now;
            
            if( diff > 0 )
            {
                sleep_time = diff > sleep_time_ ? sleep_time_ : diff;
                if( !poll )
                    sleep_time = sleep_time % 1000;
            }
            else
                sleep_time = 0;
        }
        else
        {
            sleep_time = sleep_time_;
            if( !poll )
                sleep_time = sleep_time % 1000;
        }
    }
    
    return sleep_time;
}

int LevEventLoopImpl::NewTimerID()
{
    int id;
    LevTimerCtx *pt;
    
    while( 1 )
    {
        id = timer_id_++;
        
        if( timer_id_ > LEV_MAX_TIMER_ID )
            timer_id_ = LEV_MIN_TIMER_ID;
        
        pt = FindTimer( id );
        if( !pt )
            break;
    }
    
    return id;
}

void LevEventLoopImpl::ProcFdEvents()
{
    int i, rc, cnt;
    uint32_t idx,ctx_idx;
    lev_sock_t fd;
    
    uint64_t sleep_time;
    LevIoCtx ctx;
    
#ifdef _WIN32
    int fd_cnt;
    int batch_idx;
    int batch_size;
    bool last_batch;
    FdCtx fds[FD_SETSIZE];
	LevIoCtx ctx_list[FD_SETSIZE];
    FD_SET rd_set;
    FD_SET wr_set;
    struct timeval tmv;
    bool event_trig;
#else
	FdCtx fdctx;
    struct epoll_event events[LEV_EPOLL_EVT_SIZE];
#endif
    
    
    while( fd_map_.size() )
    {
        
#ifdef _WIN32
        
        event_trig = false;
        
        batch_idx = 0;
        
        while( batch_idx < ( fd_cnt = (int)fd_map_.size() ) )
        {
            batch_size = ( fd_cnt - batch_idx ) > FD_SETSIZE ? FD_SETSIZE : ( fd_cnt - batch_idx );
            last_batch = ( batch_idx + batch_size ) >= fd_cnt ? true : false;
            
            if( last_batch )
            {
                if( event_trig )
                    sleep_time = 0;
                else
                    sleep_time = CalcSleepTime( true );
            }
            else
                sleep_time = 0;
            
            FD_ZERO( &rd_set );
            FD_ZERO( &wr_set );
            
            cnt = batch_size;
			LoadFds(batch_idx, batch_idx + batch_size -1, fds, ctx_list);
            batch_idx += batch_size;
            
            for( i = 0; i < cnt ; i++ )
            {
				ctx = ctx_list[i];
                fd  = ctx.fd;
                
                if( ctx.WatchRead )
                    FD_SET( fd, &rd_set );
                
                if(ctx.WatchWrite )
                    FD_SET( fd, &wr_set );
            }
            
            Usec2Tmv( sleep_time, tmv );
            
            rc = select( 0, &rd_set, &wr_set, NULL, &tmv );
            if( rc <= 0 )
                continue;
            
            event_trig = true;
            
            cnt = batch_size;
            
            for( i = 0; i < cnt; i++ )
            {
                ctx     = ctx_list[i];
                idx     = ctx.idx;
                fd      = ctx.fd;
                ctx_idx = fds[i].ctx_idx;
                
                if( FD_ISSET( fd, &rd_set ) )
                {
                    ctx = ctx_vec_[ctx_idx];
                    
                    if( !ctx.flag )
                        continue;
                    
                    if( ctx.idx != idx )
                        continue;
                    
                    if( ctx.WatchRead )
                        ctx.ReadCB( this, fd, ctx.ReadData );
                }
                
                //it's possible fd watcher already delete on read event call back,
                //so it's need to check if fd in watch list.
                if( FD_ISSET( fd, &wr_set ) )
                {
                    ctx = ctx_vec_[ctx_idx];
                    
                    if( !ctx.flag )
                        continue;
                    
                    if( ctx.idx != idx )
                        continue;
                    
                    if( ctx.WatchWrite )
                        ctx.WriteCB( this, fd, ctx.WriteData );
                }
            }
        }
        
        ProcCustTask();
        
        if( !event_trig )
            break;
        
#else //linux, epoll
        
        sleep_time = CalcSleepTime( true );
        
        rc = epoll_wait( epoll_fd_, events, LEV_EPOLL_EVT_SIZE, sleep_time / 1000 );
        if( rc <= 0 )
            break;
        
        cnt = rc;
        
        for( i = 0; i < cnt; i++ )
        {
            memcpy( &fdctx, &events[i].data, sizeof(fdctx) );
            
            idx     = fdctx.idx;
            ctx_idx = fdctx.ctx_idx;
            
            
            if( events[i].events & EPOLLIN )
            {
                ctx = ctx_vec_[ctx_idx];
                fd  = ctx.fd;
                
                if( !ctx.flag )
                    continue;
                
                if( ctx.idx != idx )
                    continue;
                
                if( ctx.WatchRead )
                    ctx.ReadCB( this, fd, ctx.ReadData );
            }
            
            //it's possible fd watcher already delete on read event call back,
            //so it's need to check if fd in watch list.
            if( events[i].events & EPOLLOUT )
            {
                ctx = ctx_vec_[ctx_idx];
                fd  = ctx.fd;
                
                if( !ctx.flag )
                    continue;
                
                if( ctx.idx != idx )
                    continue;
                
                if( ctx.WatchWrite )
                    ctx.WriteCB( this, fd, ctx.WriteData );
            }
        }
        
        ProcCustTask();
#endif
        
        if( !LevLoopRun )
            break;
    }
    
}

void LevEventLoopImpl::ProcCustFunc()
{
    int i,cnt;
    LevCustFuncCallback cb;
    LevCustFuncCtx cust_func[LEV_CUST_FUNC_SIZE];
    void* data;
    
    cnt = cust_func_cnt_;
    memcpy( cust_func, cust_func_, cnt*sizeof(LevCustFuncCtx) );
    
    for( i = 0; i < cnt; i++ )
    {
        cb   = cust_func[i].cb;
        data = cust_func[i].data;
        if( cb )
            cb( this, data );
    }
    
}

void LevEventLoopImpl::ProcCustTask()
{
    LevCustFuncCtx task;
    LevCustFuncCallback cb;
    
    while( cust_task_.size() )
    {
        auto it = cust_task_.begin();
        task = *it;
        
        cb = task.cb;
        if( cb )
            cb( this, task.data );
        
        cust_task_.pop_front();
    }
    
}

void LevEventLoopImpl::Run()
{
    int fd_cnt;
    uint64_t sleep_time;

    
    LevInstance ++;

    while( LevLoopRun && run_ )
    {
        fd_cnt = (int)fd_map_.size();
        
        if( fd_cnt + timer_cnt_ + cust_func_cnt_ == 0 )
        {
            UsecSleep( LEV_MAX_WAIT_TIME );
            continue;
        }
        
        //process fd io events
        ProcFdEvents();
        
        //process customer functions
        if( cust_func_cnt_ )
            ProcCustFunc();
        
        ProcCustTask();
        
        //sleep only on high latency mode
        if( !low_latency_ )
        {
            //epoll_wait wait with milliseconds,
            //so it's possible to sleep after call of epoll_wait
            sleep_time = CalcSleepTime( false );
            if( sleep_time )
                UsecSleep( sleep_time );
        }
        
        //process timer
        ProcTimerEvents();
        ProcCustTask();
        
    }
    
    LevInstance --;
    
}

void LevEventLoopImpl::ProcTimerEvents()
{
    int id;
    uint64_t now;
    LevTimerCtx *ctx;
    
    while( timer_cnt_ )
    {
        
        now = UsecNow();
        
        if( now < timer_trig_time_ )
            break;
        
        ctx = find_timer_buf();
        id = ctx->TimerID;
        
        ctx->TimerCB( this, id, ctx->Data );
        
        //in the timer callback, it's possible the timer already delete
        if( timer_cnt_ == 0 )
            break;
            
        ctx = find_timer_buf();
        if( id != ctx->TimerID )
            continue;
        
        if( ctx->Interval == 0 ) //timer Interval is 0, only trig once
        {
            DeleteTimer( id );
        }
        else
        {
            ctx->TrigTime += ctx->Interval;
            qsort( ctx, timer_cnt_, sizeof(LevTimerCtx), CompTimer );
            timer_trig_time_ = ctx->TrigTime;
        }
    }
    
}

void LevEventLoopImpl::Stop()
{
    run_ = false;
}

bool LevEventLoopImpl::AddNewTimer( LevTimerCtx ctx )
{
    LevTimerCtx *pt;
    
    if( timer_cnt_ == LEV_OBJ_BUF_SIZE )
    {
        if( timer_ext_buf_size_ == 0 )
        {
            
            pt = (LevTimerCtx*)malloc( sizeof(LevTimerCtx) * LEV_OBJ_BUF_SIZE * 2 );
            if( !pt )
                return false;
            
            timer_ext_buf_size_ = LEV_OBJ_BUF_SIZE * 2;
            
            ext_timer_buf_ = pt;
        }
        
        memcpy( ext_timer_buf_, timer_buf_, LEV_OBJ_BUF_SIZE * sizeof(LevTimerCtx) );
        
    }
    
    if( timer_cnt_ > LEV_OBJ_BUF_SIZE && timer_cnt_ == timer_ext_buf_size_ )
    {
        pt = (LevTimerCtx*)malloc( sizeof(LevTimerCtx) * timer_ext_buf_size_ * 2 );
        if( !pt )
            return false;
        
        memcpy( pt, ext_timer_buf_, sizeof(LevTimerCtx) * timer_ext_buf_size_ );
        
        timer_ext_buf_size_ = timer_ext_buf_size_ * 2;
        
        free( ext_timer_buf_ );
        ext_timer_buf_ = pt;
        
    }
    
    if( timer_cnt_ < LEV_OBJ_BUF_SIZE )
        pt = timer_buf_;
    else
        pt = ext_timer_buf_;
    
    pt[timer_cnt_] = ctx;
    timer_cnt_++;
    
    qsort( pt, timer_cnt_, sizeof(LevTimerCtx), CompTimer );
    
    return true;
}

bool LevEventLoopImpl::AddIoWatcher( lev_sock_t fd, int event, LevIoCallback cb, void* data )
{
    
    LevIoCtx ctx;
    int ctx_idx;
#ifdef __linux__
    int rc;
    struct epoll_event evt;
	FdCtx fdctx;
#endif

    if( !cb )
        return false;
    
    switch( event )
    {
        case LEV_IO_EVENT_READ:
        case LEV_IO_EVENT_WRITE:
            break;
        
        default:
            return false;
    }

    auto it = fd_map_.find( fd );
    if( it != fd_map_.end() )
    {
        ctx_idx = it->second;
        ctx = ctx_vec_[ctx_idx];
        
        if( event == LEV_IO_EVENT_READ )
        {
            if( ctx.WatchRead )
                return false;
                
            ctx.WatchRead = 1;
            ctx.ReadCB    = cb;
            ctx.ReadData  = data;
        }
        
        if( event == LEV_IO_EVENT_WRITE )
        {
            if( ctx.WatchWrite )
                return false;
                
            ctx.WatchWrite = 1;
            ctx.WriteCB    = cb;
            ctx.WriteData  = data;
        }

#ifdef __linux__
        
        fdctx.ctx_idx  = ctx_idx;
        fdctx.idx = ctx.idx;
        memcpy( &evt.data, &fdctx, sizeof(fdctx) );
        
        evt.events = 0;
        
        if( ctx.WatchRead )
            evt.events = evt.events | EPOLLIN;
            
        if( ctx.WatchWrite )
            evt.events = evt.events | EPOLLOUT;
        
        
        rc = epoll_ctl( epoll_fd_, EPOLL_CTL_MOD, fd, &evt );
        if( rc == -1 )
        {
            //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
            return false;
        }

#endif
        ctx_vec_[ctx_idx] = ctx;
        
    }
    else
    {
        memset( &ctx, 0, sizeof(ctx) );
        ctx_idx = FindFreePos();
        if( ctx_idx == -1 )
        {
            try{
                ctx_vec_.push_back( ctx );
            }
            catch(...)
            {
                return false;
            }
            
            ctx_idx = (int)ctx_vec_.size() - 1;
            
        }
        
        ctx.flag = 1;
        ctx.fd   = fd;
        ctx.idx  = fd_idx_++;
        
        if( event == LEV_IO_EVENT_READ )
        {
            ctx.WatchRead = 1;
            ctx.ReadCB    = cb;
            ctx.ReadData  = data;
        }
        
        if( event == LEV_IO_EVENT_WRITE )
        {
            ctx.WatchWrite = 1;
            ctx.WriteCB    = cb;
            ctx.WriteData  = data;
        }
        
        fd_map_[fd] = ctx_idx;
        ctx_vec_[ctx_idx] = ctx;
        
#ifdef __linux__

        fdctx.ctx_idx  = ctx_idx;
        fdctx.idx = ctx.idx;
        
        memcpy( &evt.data, &fdctx, sizeof(fdctx) );
        
        evt.events = 0;
        
        if( ctx.WatchRead )
            evt.events = evt.events | EPOLLIN;
            
        if( ctx.WatchWrite )
            evt.events = evt.events | EPOLLOUT;
        
        rc = epoll_ctl( epoll_fd_, EPOLL_CTL_ADD, fd, &evt );
        if( rc == -1 )
        {
            //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
            fd_map_.erase( fd );
            ctx.flag = 0;
            ctx_vec_[ctx_idx] = ctx;
            return false;
        }
        
#endif
        
    }
    
    return true;
}

bool LevEventLoopImpl::DeleteIoWatcher( lev_sock_t fd, int event )
{
    LevIoCtx ctx;
    int ctx_idx;
#ifdef __linux__
    int rc, op;
    FdCtx fdctx;
    struct epoll_event evt;
#endif
    
    switch( event )
    {
        case LEV_IO_EVENT_READ:
        case LEV_IO_EVENT_WRITE:
            break;
        
        default:
            return false;
    }
    
    auto it = fd_map_.find( fd );
    
    if( it == fd_map_.end() )
        return false;

    ctx_idx = it->second;
    ctx = ctx_vec_[ctx_idx];
    
#ifdef LEV_CHECK_VALID_FD

    if( !IsValidFd( fd ) )
    {
        fd_map_.erase( fd );
        return false;
    }
        
#endif
    
    if( event == LEV_IO_EVENT_READ )
    {
        if( !ctx.WatchRead )
            return false;
            
        ctx.WatchRead = 0;
    }
    
    if( event == LEV_IO_EVENT_WRITE )
    {
        if( !ctx.WatchWrite )
            return false;
        
        ctx.WatchWrite = 0;
    }
    
        
#ifdef __linux__
    
    evt.events = 0;
    if( ctx.WatchRead )
        evt.events = evt.events | EPOLLIN;
    
    if( ctx.WatchWrite )
        evt.events = evt.events | EPOLLOUT;
    
    fdctx.ctx_idx  = ctx_idx;
    fdctx.idx      = ctx.idx;
    memcpy( &evt.data, &fdctx, sizeof(fdctx) );
    
    if( evt.events )
        op = EPOLL_CTL_MOD;
    else
        op = EPOLL_CTL_DEL;
        
    rc = epoll_ctl( epoll_fd_, op, fd, &evt );
    if( rc == -1 )
    {
        //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
        return false;
    }
    
#endif
    
    //delete fd from watch list
    if( !ctx.WatchRead && !ctx.WatchWrite )
    {
        fd_map_.erase( fd );
        ctx.flag = 0;
        ctx_vec_[ctx_idx] = ctx;
    }
    else
        ctx_vec_[ctx_idx] = ctx;
    
    return true;
}

bool LevEventLoopImpl::DeleteIoWatcher( lev_sock_t fd )
{
    int ctx_idx;
#ifdef __linux__
    struct epoll_event evt;
#endif

    auto it = fd_map_.find( fd );
    
    if( it == fd_map_.end() )
        return false;
    
    ctx_idx = it->second;
#ifdef LEV_CHECK_VALID_FD

    if( !IsValidFd( fd ) )
    {
        fd_map_.erase( fd );
        return false;
    }
        
#endif

#ifdef __linux__
    
    if( epoll_ctl( epoll_fd_, EPOLL_CTL_DEL, fd, &evt ) == -1 )
    {
        //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
        return false;
    }
        
#endif

    fd_map_.erase( fd );
    ctx_vec_[ctx_idx].flag = 0;
    
    return true;
}

bool LevEventLoopImpl::AddTimerWatcher( double start, double interval, LevTimerCallback cb, void* data, int& id )
{
    LevTimerCtx ctx, *pt;
    uint64_t now;
    
    id = LEV_INVALID_TIMER_ID;
    
    if( cb == NULL )
        return false;
    
    now = UsecNow();
    
    id = NewTimerID();
    ctx.TimerID  = id;
    ctx.TrigTime = now + (uint64_t)(start * 1000000 );
    ctx.Interval = (uint64_t)(interval * 1000000);
    ctx.TimerCB  = cb;
    ctx.Data     = data;
    
    if( !AddNewTimer( ctx ) )
        return false;
    
    pt = find_timer_buf();
    timer_trig_time_ = pt[0].TrigTime;
    
    return true;
}

bool LevEventLoopImpl::DeleteTimerWatcher( int id )
{
    LevTimerCtx *ctx;
    
    if( id < LEV_MIN_TIMER_ID || id > LEV_MAX_TIMER_ID )
        return false;
    
    if( timer_cnt_ == 0 )
        return false;
        
    ctx = FindTimer( id );
        
    if( !ctx )
        return false;
    
    DeleteTimer( id );
    
    return true;
}

bool LevEventLoopImpl::AddCustFunc( LevCustFuncCallback cb, void* data )
{
    int i;
    
    if( cb == NULL )
        return false;
        
    if( cust_func_cnt_ == LEV_CUST_FUNC_SIZE )
        return false;
    
    for( i = 0; i < cust_func_cnt_; i++ )
    {
        if( cust_func_[i].cb == cb && cust_func_[i].data == data )
            return false;
    }
    
    cust_func_[cust_func_cnt_].cb   = cb;
    cust_func_[cust_func_cnt_].data = data;
    
    cust_func_cnt_++;
    
    return true;
}

bool LevEventLoopImpl::AddCustTask( LevCustFuncCallback cb, void* data )
{
    LevCustFuncCtx task;
    if( cb == NULL )
        return false;
    
    task.cb = cb;
    task.data = data;
    
    cust_task_.push_back( task );

    
    return true;
}

bool LevEventLoopImpl::DeleteCustFunc( LevCustFuncCallback cb, void* data )
{
    int i;
    int idx;
    
    if( cb == NULL )
        return false;
    
    if( cust_func_cnt_ == 0 )
        return false;
        
    idx = -1;
    
    for( i = 0; i < cust_func_cnt_; i++ )
    {
        if( cust_func_[i].cb == cb && cust_func_[i].data == data )
        {
            idx = i;
            break;
        }
    }
    
    if( idx == -1 )
        return false;
    
    memmove( cust_func_ + idx, cust_func_ + idx + 1, sizeof(LevCustFuncCtx)*(cust_func_cnt_-idx-1) );
    cust_func_cnt_ --;
    
    memset( cust_func_ + cust_func_cnt_, 0, sizeof(LevCustFuncCtx)*( LEV_CUST_FUNC_SIZE - cust_func_cnt_) );
    
    return true;
    
}


void LevEventLoopImpl::Close( lev_sock_t fd )
{
    
    DeleteIoWatcher( fd );
    
#ifdef _WIN32
    
    closesocket( fd );

#else
    
    close( fd );
    
#endif
    
}

LevEventLoop* LevCreateEventLoop( bool LowLatency )
{
    LevEventLoopImpl* loop;
    
    try
    {
        loop = new LevEventLoopImpl( LowLatency );
    }
    catch( ... )
    {
        return NULL;
    }
    
    
    if( !loop->Init() )
    {
        delete loop;
        return NULL;
    }
    
    return loop;
}


#ifdef _WIN32

BOOL WINAPI HandlerRoutine( DWORD type )
{
    if( type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT )
    {
        LevLoopRun = false;
        while( LevInstance.load() )
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return true;
    }
    
    return false;
}

#endif

bool LevInitEnvironment()
{
    
#ifdef _WIN32
    
    static bool init = false;
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    
    if( init )
        return true;
    
    wVersionRequested = MAKEWORD( 2, 2 );
    err = WSAStartup( wVersionRequested, &wsaData );
    if( err )
    {
        //printf( "WSAStartup failed!\n" );
        return false;
    }
    
    SetConsoleCtrlHandler( HandlerRoutine, true );
    
    init = true;
    
#endif
    
    return true;
}


int CompTimer( const void* data1, const void* data2 )
{
    LevTimerCtx *ctx1, *ctx2;
    
    ctx1 = (LevTimerCtx*)data1;
    ctx2 = (LevTimerCtx*)data2;
    
    return ctx1->TrigTime > ctx2->TrigTime ? 1 : ( ctx1->TrigTime == ctx2->TrigTime ? 0 : -1 );
}

void LevStopAllEventLoop()
{
    LevLoopRun = false;
    
}

uint64_t UsecNow()
{
    uint64_t usec_epoch;
    
#ifdef _WIN32
    
    struct timespec tms;
    timespec_get( &tms, TIME_UTC );
    usec_epoch = tms.tv_sec;
    usec_epoch *= 1000000;
    usec_epoch += tms.tv_nsec/1000;
    
#else

    struct timeval tmv;
    gettimeofday( &tmv, NULL );
    usec_epoch = tmv.tv_sec;
    usec_epoch *= 1000000;
    usec_epoch += tmv.tv_usec;

#endif

    return usec_epoch;
}


void UsecSleep( uint64_t usec )
{
    
#ifdef _WIN32
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
#else
    usleep( usec );
#endif
    
}

#ifdef LEV_CHECK_VALID_FD
bool IsValidFd( lev_sock_t fd )
{
#ifdef _WIN32
    int type;
    int optlen;
    
    optlen = sizeof(int);
    if( ( getsockopt( fd, SOL_SOCKET, &type, &optlen ) == SOCKET_ERROR ) && ( WSAGetLastError() == WSAENOTSOCK ) )
        return false;
    else
        return true;
#else

    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;

#endif
}
#endif


bool LevSetNonblocking( lev_sock_t fd )
{
    
#ifdef _WIN32
    
    unsigned long ul = 1;
    int           nRet;
    nRet = ioctlsocket( fd, FIONBIO, (unsigned long *) &ul );
    if (nRet == SOCKET_ERROR)
        return false;
    
    return true;
    
#else
    
    int flags, rc ;
    if (-1 == (flags = fcntl(fd, F_GETFL, 0))) {
        flags = 0;
    }
    
    rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if( rc == -1 )
        return false;
    
    return true;
    
#endif

}


#ifdef _WIN32
void Usec2Tmv( uint64_t usec, struct timeval &tmv )
{
    
    tmv.tv_sec  = (int)(usec / 1000000);
    tmv.tv_usec = (int)(usec % 1000000);
}
#endif

    
void LevEventLoopImpl::DeleteTimer( int id )
{
    int i, idx;
    LevTimerCtx *pt, *base;
    
    if( timer_cnt_ == 0 )
        return;
    
    base = find_timer_buf();
    
    for( i = 0; i < timer_cnt_; i++ )
    {
        if( base[i].TimerID == id )
        {
            pt = base + i;
            idx = i;
            memmove( pt, pt+1, sizeof(LevTimerCtx)* (timer_cnt_-1-idx) );
            
            timer_cnt_ --;
            
            if( timer_cnt_ == LEV_OBJ_BUF_SIZE )
                memcpy( timer_buf_, ext_timer_buf_, sizeof(LevTimerCtx)*LEV_OBJ_BUF_SIZE );
            
            if( timer_cnt_ )
            {
                pt = find_timer_buf();
                timer_trig_time_ = pt[0].TrigTime;
            }
            else
                timer_trig_time_ = 0;
                
            return;
            
        }
    }
}

void LevEventLoopImpl::CloseAll()
{
    lev_sock_t fd;
    
    while( fd_map_.size() )
    {
        auto it = fd_map_.begin();
        
        fd = it->first;
        
        Close( fd );
    }
    
}

LevEventLoopImpl __default_lev_loop( false );

LevEventLoop* LevGetDefaultLoop()
{
    static bool init = false;
    static bool init_ok = false;
    
    if( !init )
    {
        init = true;
        if( __default_lev_loop.Init() )
            init_ok = true;
        else
            init_ok = false;
    }
    
    return init_ok?  &__default_lev_loop : NULL;
}


