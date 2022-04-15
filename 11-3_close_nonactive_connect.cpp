#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "11-2_uppper_list_timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMERSLOT 1

static int pipefd[2];

/* 使用 11-2 升序链表来管理定时器 */
static sort_timer_lst timer_lst;
static int epollfd = 0;

int setnonblocking( int fd ) {
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_operation );
    return old_operation;
}

void addfd( int epollfd, int fd ) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLET | EPOLLIN;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

void sig_handler( int sig ) {
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], (char*) &msg, 1, 0 );
    errno = save_errno;
}

void addsig( int sig ) {
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_flags |= SA_RESTART;
    sa.sa_handler = sig_handler;
    sigfillset( &sa.sa_mask );
    assert ( sigaction( sig, &sa, NULL ) != -1 );
}

void timer_handler() {
    /* 定时处理任务，实际上就是调用 tick 函数 */
    timer_lst.tick();
    /* 因为一次 alarm 调用只会引起一次 SIGALRM 信号，所以我们要重新定时，
        以不断出发 SIGALRM 信号 */
    alarm( TIMERSLOT );
}

/* 定时器回调函数，它删除非活动连接 socket 上的注册事件，并关闭之 */
void cb_func( client_data* user_data ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
    assert( user_data );
    close( user_data->sockfd );
    printf( "close fd %d\n", user_data->sockfd );
}



int main( int argc, char* argv[] ) {
    if ( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename ( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    struct sockaddr_in address;
    bzero( &address, sizeof ( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert ( listenfd >= 0 );

    int ret = 0;
    ret = bind( listenfd, ( sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );
    printf( "bind success\n" );

    ret = listen( listenfd, 5 ); 
    assert( ret != -1 );
    printf( "listen success\n" );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd ); // 先把服务器 socket 监听起来

    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    setnonblocking( pipefd[1] );
    addfd( epollfd, pipefd[0] );

    /* 设置信号处理函数 */
    addsig( SIGALRM );
    addsig( SIGTERM );
    bool stop_server = false;

    client_data* users = new client_data[ FD_LIMIT ]; // 提前就准备好了 65536 个客户的数据存储空间
    bool timeout = false;
    alarm( TIMERSLOT ); /* 开始定时 */

    while ( !stop_server ) {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && errno != EINTR ) {
            printf( "epoll failure\n" );
            break;
        }

        for (int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd;
            /* 处理新的客户连接 */
            if ( sockfd == listenfd ) {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof( client_address );
                int connfd = accept( listenfd, ( sockaddr* ) &client_address, &client_addresslength );
                addfd( epollfd, connfd ); // 开始监听这个连接传来的数据
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                /* 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将
                    定时器添加到链表 timer_list 中 */
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur  = time( NULL );
                timer->expire = cur + 3 * TIMERSLOT; //设置为 3 个周期内不断开就关闭
                users[connfd].timer = timer;
                timer_lst.add_timer( timer ); // 就是在这里添加进去 时间队列 里
            }
            /* 处理信号 */
            else if ( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if ( ret = -1 ) {
                    // handle the errno
                    continue;
                }
                else if( ret == 0 ) {
                    continue;
                }
                else {
                    for ( int i = 0; i < ret; ++i ) {
                        switch ( signals[i] )
                        {
                        case SIGALRM:
                        {
                            /* 用 timeout 变量标记有定时任务需要处理，但不立即处理定时任务。这是因为定时任务的优先级不是很高，
                            我们优先处理其他更重要的任务 */
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                            printf( "immediately close the server for SIGTERM\n" );
                        }
                        }
                    }
                }
            }
            /* 处理客户连接上接收到的数据 */
            else if ( events[i].events & EPOLLIN ) {
                memset( users[sockfd].buf, '\0', BUFFER_SIZE );
                ret = recv( sockfd, users[sockfd].buf, BUFFER_SIZE - 1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret , users[sockfd].buf ,sockfd );
                
                util_timer* timer = users[sockfd].timer;
                if ( ret < 0 ) {
                    /* 如果发生读出错，则关闭连接，并移除其对应的连接器 */
                    if ( errno != EAGAIN ) {
                        cb_func( &users[sockfd] );
                        if ( timer ) { // 如果是在定时器里被调用的时候删除，会自动从链表中删除，因此这时候要自己手动从链表中删除
                            timer_lst.del_timer( timer );
                        }
                    }
                }
                else if ( ret == 0 ) {
                    /* 如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器 */
                    cb_func( &users[sockfd] );
                    if ( timer ) {
                        timer_lst.del_timer( timer );
                    }
                }
                else {
                    /* 如果某个客户连接上有数据可读，则我们要调整连接对应的定时器，以延迟该连接被关闭的时间 */
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMERSLOT;
                        printf( "adjust timer once\n" );
                        timer_lst.adjust_timer( timer );
                    }
                    else {
                    // other,没有设置定时器
                    }
                }    
            }
        }
        
        /* 最后处理定时事件，因为 I/O 事件有更高的优先级。 当然，这样做将导致
        定时任务不能精准地按照预期的时间执行 */
        if ( timeout ) {
            timer_handler();
            timeout = false;
        } 
    }

    close( listenfd );
    close( pipefd[0] );
    close( pipefd[1] );
    delete []users;
    return 0; 
}