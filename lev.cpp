#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <atomic>
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

//#define LEV_PERF_TEST 1
//#define LEV_CHECK_VALID_FD  1


#ifdef LEV_PERF_TEST
inline struct timespec nsec_now()
{
    struct timespec tms;
    
#ifdef _WIN32
    timespec_get( &tms, TIME_UTC );
#else
    clock_gettime( CLOCK_REALTIME, &tms );
#endif

    return tms;
}

inline uint64_t nsec_diff( struct timespec t1, struct timespec t2 )
{
    return ((uint64_t)( t2.tv_sec - t1.tv_sec ))* 1000000000 + ( t2.tv_nsec - t1.tv_nsec );
}

uint64_t total_time_spend;
int      total_times;
int write_log( const char* fmt, ... );
#endif




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
    lev_sock_t fd;
    uint32_t idx;
}FdCtx;

typedef struct LevIoCtx{
    
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

static int comp_timer( const void* data1, const void* data2 );

static uint64_t usec_now();

static void usec_sleep( uint64_t usec );

#ifdef LEV_CHECK_VALID_FD
static bool is_valid_fd( lev_sock_t fd );
#endif

#ifdef _WIN32
static void usec2tmv( uint64_t usec, struct timeval &tmv );
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
    
    void load_fds( int start_idx, int end_idx, lev_sock_t fd_list[], LevIoCtx ctx_list[] );
    
    bool add_new_timer( LevTimerCtx ctx);
    
    void proc_fd_events();
    
    void proc_cust_func();
    
    void proc_cust_task();
    
    void proc_timer_events();
    
    void delete_timer( int id );
    
    uint64_t calc_sleep_time( bool poll );
    
    LevTimerCtx* find_timer( int id );
        
    int new_timer_id();
    
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
    
    int cust_task_cnt_;
    
    LevTimerCtx* ext_timer_buf_;
    
    LevTimerCtx  timer_buf_[LEV_OBJ_BUF_SIZE];
    
    LevCustFuncCtx cust_func_[LEV_CUST_FUNC_SIZE];
    
    LevCustFuncCtx cust_task_[LEV_CUST_TASK_SIZE];
    
    std::map<lev_sock_t, LevIoCtx> fd_table_;
    
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
    cust_task_cnt_ = 0;
    
    memset( cust_func_, 0, sizeof(cust_func_) );
    
}

LevEventLoopImpl::~LevEventLoopImpl()
{
    
    if( ext_timer_buf_ )
        free( ext_timer_buf_ );
    
#ifndef _WIN32

    if( epoll_fd_ != -1 )
    {
        close( epoll_fd_ );
    }

#endif
    
}

void LevEventLoopImpl::load_fds( int start_idx, int end_idx, lev_sock_t fd_list[], LevIoCtx ctx_list[] )
{
    int idx = 0;
    int cnt = 0;
    
    for( auto it = fd_table_.begin(); it != fd_table_.end(); it++ )
    {
        if( idx >= start_idx && idx <= end_idx )
        {
            fd_list[idx]  = it->first;
            ctx_list[idx] = it->second;
            
            idx++;
            cnt++;
        }
    }
    
    
}

void LevEventLoopImpl::SetSleepTime( int miliseoncds )
{
    if( miliseoncds <= 0 || miliseoncds > LEV_MAX_WAIT_TIME/1000 )
        return ;
    
    sleep_time_ = miliseoncds * 1000;
    
}

bool LevEventLoopImpl::Init()
{
    
#ifndef _WIN32
    
    epoll_fd_ = epoll_create( 100 );
    if( epoll_fd_ == -1 )
    {
        //write_log( "[ERR] create epoll failed! err:%s\n", strerror( errno ) );
        return false;
    }

#endif
    
    return true;
}

LevTimerCtx* LevEventLoopImpl::find_timer( int id )
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

uint64_t LevEventLoopImpl::calc_sleep_time( bool poll )
{
    int64_t  diff;
    uint64_t now,sleep_time;
    
    if( low_latency_ )
    {
        sleep_time = 0;
    }
    else
    {
        now = usec_now();
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

int LevEventLoopImpl::new_timer_id()
{
    int id;
    LevTimerCtx *pt;
    
    while( 1 )
    {
        id = timer_id_++;
        
        if( timer_id_ > LEV_MAX_TIMER_ID )
            timer_id_ = LEV_MIN_TIMER_ID;
        
        pt = find_timer( id );
        if( !pt )
            break;
    }
    
    return id;
}

void LevEventLoopImpl::proc_fd_events()
{
    int i, rc, cnt;
    uint32_t idx;
    lev_sock_t fd;
    
    uint64_t sleep_time;
    LevIoCtx ctx;
    
#ifdef _WIN32
    int fd_cnt;
    int batch_idx;
    int batch_size;
    bool last_batch;
    FdCtx fds[FD_SETSIZE];
	lev_sock_t fd_list[FD_SETSIZE];
	LevIoCtx   ctx_list[FD_SETSIZE];
    FD_SET read_set;
    FD_SET write_set;
    struct timeval tmv;
    bool event_trig;
#else
	FdCtx fdctx;
    struct epoll_event events[10];
#endif
    
    
    while( fd_table_.size() )
    {
        
#ifdef _WIN32
        
        event_trig = false;
        
        batch_idx = 0;
        
        fd_cnt = (int)fd_table_.size();
        
        while( batch_idx < fd_cnt )
        {
            batch_size = ( fd_cnt - batch_idx ) > FD_SETSIZE ? FD_SETSIZE : ( fd_cnt - batch_idx );
            last_batch = ( batch_idx + batch_size ) >= fd_cnt ? true : false;
            
            if( last_batch )
                sleep_time = calc_sleep_time( true );
            else
                sleep_time = 0;
            
            FD_ZERO( &read_set );
            FD_ZERO( &write_set );
            
            cnt = batch_size;
			load_fds(batch_idx, batch_idx + batch_size, fd_list, ctx_list);


            for( i = 0; i < cnt ; i++ )
            {
				fds[i].fd = fd_list[i];
				fds[i].idx = ctx_list[i].idx;
				ctx = ctx_list[i];
                    
                if( ctx.WatchRead )
                    FD_SET( fds[i].fd, &read_set );
                
                if(ctx.WatchWrite )
                    FD_SET( fds[i].fd, &write_set );
            }
            
            usec2tmv( sleep_time, tmv );
            
            rc = select( 0, &read_set, &write_set, NULL, &tmv );
            if( rc <= 0 )
            {
                batch_idx += batch_size;
                continue;
            }
            
            event_trig = true;
            
            cnt = batch_size;
            
            for( i = 0; i < cnt; i++ )
            {
                fd  = fds[i].fd;
                idx = fds[i].idx;
                
                if( FD_ISSET( fd, &read_set ) )
                {
                    auto it = fd_table_.find( fd );
                    
                    if( it == fd_table_.end() )
                        continue;
                    
                    ctx = it->second;
                    
                    if( idx != ctx.idx )
                        continue;

                    if( ctx.WatchRead )
                        ctx.ReadCB( this, fd, ctx.ReadData );
                }
                
                if( FD_ISSET( fd, &write_set ) )
                {
                    
                    auto it = fd_table_.find( fd );
                    
                    if( it == fd_table_.end() )
                        continue;
                    
                    ctx = it->second;

                    if( idx != ctx.idx )
                        continue;
                        
                    if( ctx.WatchWrite )
                        ctx.WriteCB( this, fd, ctx.WriteData );
                }
            }
            
            batch_idx += batch_size;
        }
        
        proc_cust_task();
        
        if( !event_trig )
            break;
        

#else //linux, epoll

        sleep_time = calc_sleep_time( true );
        
        rc = epoll_wait( epoll_fd_, events, sizeof(events)/sizeof(struct epoll_event), sleep_time / 1000 );
        if( rc <= 0 )
            break;

        cnt = rc;
        
        for( i = 0; i < cnt; i++ )
        {
            memcpy( &fdctx, &events[i].data, sizeof(fdctx) );
            
            fd  = fdctx.fd;
            idx = fdctx.idx;
            
            if( events[i].events & EPOLLIN )
            {

#ifdef LEV_PERF_TEST
                auto t1 = nsec_now();
#endif
                auto it = fd_table_.find( fd );

#ifdef LEV_PERF_TEST
                auto t2 = nsec_now();
                uint64_t diff = nsec_diff( t1, t2 );
                total_time_spend += diff;
                total_times++;
                if( total_times % 100 == 0 )
                {
                    write_log( "100 loop time spend: %" PRIu64 " nanoseconds!", total_time_spend );
                    total_time_spend = 0;
                    total_times = 0;
                }
#endif
                if( it == fd_table_.end() )
                    continue;
                
                ctx = it->second;
                    
                if( idx != ctx.idx )
                    continue;
                
                if( ctx.WatchRead )
                    ctx.ReadCB( this, fd, ctx.ReadData );
            }
            
            //it's possible fd watcher already delete on read event call back,
            //so it's need to check if fd in watch list.
            if( events[i].events & EPOLLOUT )
            {
                auto it = fd_table_.find( fd );
                
                if( it == fd_table_.end() )
                    continue;
                
                ctx = it->second;
                
                if( idx != ctx.idx )
                    continue;
                
                if( ctx.WatchWrite )
                    ctx.WriteCB( this, fd, ctx.WriteData );
            }
        }
        proc_cust_task();
#endif
        
        if( !LevLoopRun )
            break;
    }
        
}

void LevEventLoopImpl::proc_cust_func()
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

void LevEventLoopImpl::proc_cust_task()
{
    int i;
    LevCustFuncCallback cb;
    void* data;
    
    if( !cust_task_cnt_ )
        return;
    
    for( i = 0; i < cust_task_cnt_; i++ )
    {
        cb   = cust_task_[i].cb;
        data = cust_task_[i].data;
        if( cb )
            cb( this, data );
    }
    
    cust_task_cnt_ = 0;
    
    
}

void LevEventLoopImpl::Run()
{
    int fd_cnt;
    uint64_t sleep_time;

    
    LevInstance ++;

    while( LevLoopRun && run_ )
    {
        fd_cnt = (int)fd_table_.size();
        
        if( fd_cnt + timer_cnt_ + cust_func_cnt_ == 0 )
        {
            usec_sleep( LEV_MAX_WAIT_TIME );
            continue;
        }
        
        //process fd io events
        proc_fd_events();
        
        //process customer functions
        if( cust_func_cnt_ )
            proc_cust_func();
        
        proc_cust_task();
        
        //sleep only on high latency mode
        if( !low_latency_ )
        {
            //epoll_wait wait with milliseconds,
            //so it's possible to sleep after call of epoll_wait
            sleep_time = calc_sleep_time( false );
            if( sleep_time )
                usec_sleep( sleep_time );
        }
        
        //process timer
        proc_timer_events();
        proc_cust_task();
        
    }
    
    LevInstance --;
    
}

void LevEventLoopImpl::proc_timer_events()
{
    int id;
    uint64_t now;
    LevTimerCtx *ctx;
    
    while( timer_cnt_ )
    {
        
        now = usec_now();
        
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
            delete_timer( id );
        }
        else
        {
            ctx->TrigTime += ctx->Interval;
            qsort( ctx, timer_cnt_, sizeof(LevTimerCtx), comp_timer );
            timer_trig_time_ = ctx->TrigTime;
        }
    }
    
}

void LevEventLoopImpl::Stop()
{
    run_ = false;
}

bool LevEventLoopImpl::add_new_timer( LevTimerCtx ctx )
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
    
    qsort( pt, timer_cnt_, sizeof(LevTimerCtx), comp_timer );
    
    return true;
}

bool LevEventLoopImpl::AddIoWatcher( lev_sock_t fd, int event, LevIoCallback cb, void* data )
{
    
    LevIoCtx ctx;
    
#ifndef _WIN32
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

#ifdef LEV_CHECK_VALID_FD

    if( !is_valid_fd( fd ) )
    {
        fd_table_.erase( fd );
        return false;
    }
        
#endif

    auto it = fd_table_.find( fd );
    if( it != fd_table_.end() )
    {
        ctx = it->second;
        
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

#ifndef _WIN32
        
        fdctx.fd  = fd;
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
        fd_table_[fd] = ctx;
        
    }
    else
    {
        memset( &ctx, 0, sizeof(ctx) );
        
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
        
#ifndef _WIN32

        fdctx.fd  = fd;
        fdctx.idx = ctx.idx;
        
        memcpy( &evt.data, &fdctx, sizeof(fdctx) );
        
        evt.events = 0;
        
        if( ctx.WatchRead )
            evt.events = evt.events | EPOLLIN;
            
        if( ctx.WatchWrite )
            evt.events = evt.events | EPOLLOUT;
#endif
        fd_table_[fd] = ctx;

#ifndef _WIN32
        
        rc = epoll_ctl( epoll_fd_, EPOLL_CTL_ADD, fd, &evt );
        if( rc == -1 )
        {
            //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
            fd_table_.erase( fd );
            return false;
        }
        
#endif
        
    }
    
    return true;
}

bool LevEventLoopImpl::DeleteIoWatcher( lev_sock_t fd, int event )
{
    LevIoCtx ctx;
    
#ifndef _WIN32
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
    
    auto it = fd_table_.find( fd );
    
    if( it == fd_table_.end() )
        return false;

    ctx = it->second;
    
#ifdef LEV_CHECK_VALID_FD

    if( !is_valid_fd( fd ) )
    {
        fd_table_.erase( fd );
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
    
        
#ifndef _WIN32
    
    evt.events = 0;
    if( ctx.WatchRead )
        evt.events = evt.events | EPOLLIN;
    
    if( ctx.WatchWrite )
        evt.events = evt.events | EPOLLOUT;
    
    fdctx.fd  = fd;
    fdctx.idx = ctx.idx;
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
        fd_table_.erase( fd );
    else
        fd_table_[fd] = ctx;
    
    return true;
}

bool LevEventLoopImpl::DeleteIoWatcher( lev_sock_t fd )
{
    
#ifndef _WIN32
    struct epoll_event evt;
#endif

    auto it = fd_table_.find( fd );
    
    if( it == fd_table_.end() )
        return false;

#ifdef LEV_CHECK_VALID_FD

    if( !is_valid_fd( fd ) )
    {
        fd_table_.erase( fd );
        return false;
    }
        
#endif

#ifndef _WIN32
    
    if( epoll_ctl( epoll_fd_, EPOLL_CTL_DEL, fd, &evt ) == -1 )
    {
        //write_log( "[WRN] epoll op failed! line: %d, fd: %d, errno: %d err: %s", __LINE__, fd, errno, strerror( errno ) );
        return false;
    }
        
#endif

    fd_table_.erase( fd );
            
    return true;
}

bool LevEventLoopImpl::AddTimerWatcher( double start, double interval, LevTimerCallback cb, void* data, int& id )
{
    LevTimerCtx ctx, *pt;
    uint64_t now;
    
    id = LEV_INVALID_TIMER_ID;
    
    if( cb == NULL )
        return false;
    
    now = usec_now();
    
    id = new_timer_id();
    ctx.TimerID  = id;
    ctx.TrigTime = now + (uint64_t)(start * 1000000 );
    ctx.Interval = (uint64_t)(interval * 1000000);
    ctx.TimerCB  = cb;
    ctx.Data     = data;
    
    if( !add_new_timer( ctx ) )
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
        
    ctx = find_timer( id );
        
    if( !ctx )
        return false;
    
    delete_timer( id );
    
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
    
    if( cb == NULL )
        return false;
    
    if( cust_task_cnt_ == LEV_CUST_TASK_SIZE )
        return false;
    
    cust_task_[cust_task_cnt_].cb   = cb;
    cust_task_[cust_task_cnt_].data = data;
    
    cust_task_cnt_++;
    
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
    
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;
    
    wVersionRequested = MAKEWORD( 2, 2 );
    err = WSAStartup( wVersionRequested, &wsaData );
    if( err )
    {
        //printf( "WSAStartup failed!\n" );
        return false;
    }
    
    SetConsoleCtrlHandler( HandlerRoutine, true );
    
#endif
    
    return true;
}


int comp_timer( const void* data1, const void* data2 )
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

uint64_t usec_now()
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


void usec_sleep( uint64_t usec )
{
    
#ifdef _WIN32
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
#else
    usleep( usec );
#endif
    
}

#ifdef LEV_CHECK_VALID_FD
bool is_valid_fd( lev_sock_t fd )
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
void usec2tmv( uint64_t usec, struct timeval &tmv )
{
    
    tmv.tv_sec  = (int)(usec / 1000000);
    tmv.tv_usec = (int)(usec % 1000000);
}
#endif

    
void LevEventLoopImpl::delete_timer( int id )
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
    
    while( fd_table_.size() )
    {
        auto it = fd_table_.begin();
        
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


