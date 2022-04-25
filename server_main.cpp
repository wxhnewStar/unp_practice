#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "14-2_locker.h"
#include "15-3_threadpoll.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );


void addsig( int sig , void( handler )(int ), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if ( restart )
    {
        sa.sa_flags += SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    int ret = sigaction( sig , &sa, NULL );
    assert( ret != -1 );
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );  // 这里关闭了连接 ？
}

int main( int argc, char* argv[] )
{
    if ( argc <= 2 )
    {
        printf( "usage: #s ip_address port_number\n", basename( argv[1] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    /* 忽略sigpipe信号 */
    addsig( SIGPIPE, SIG_IGN );  // 这个信号默认处理方式是退出进程，因此我们设置为 IGN，这样子会返回-1，errno 设置为DIGPIPE

    /* 创建线程池 */
    threadpoll< http_conn >*poll = nullptr;
    try
    {
        poll = new threadpoll<http_conn>;
    }
    catch( ... )
    {
        return 1;
    }
    
    /* 预先为每个可能的客户连接分配一个 http_conn 对象 */
    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };  // 设置为关闭以后，直接返回，丢弃发送缓冲区中数据，并发送一个 RST 报文？？
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_port = htons( port );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );

    ret = bind( listenfd, (sockaddr*)&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;  // 这是一个静态变量

    while( true )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for( int i = 0; i < number; ++i )
        {
            int sockfd = events[i].data.fd;
            if ( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof( client_address );
                int connfd = accept( listenfd, &client_address, &client_addresslength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if ( http_conn::m_user_count >= MAX_FD )
                {
                    // 此时连接数超过了我们的限制，直接在连接的时候和 client 说
                    show_error( connfd, "Internal server busy" );
                }
                /* 初始化客户连接 */
                users[ connfd ].init( connfd, client_address );  // 这里面会增加用户量计数
            }
            else if ( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                // 该连接对方关闭了，或者有异常
                users[sockfd].close_conn();
            }
            else if ( events[i].events & EPOLLIN )
            {
                /* 根据读的结果，决定是将任务添加到线程池，还是关闭连接 */
                if ( users[sockfd].read() )
                {
                   if ( !poll->append( user+ sockfd )) {
                        printf( "threadpoll append failure\n" );
                   }
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if ( events[i].events & EPOLLOUT )
            {
                /* 根据写的结果，决定是否关闭连接 */
                if ( !users[sockfd].write() )
                {
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }

    close( epollfd );
    close( listenfd );
    delete []users;
    delete poll;
    return 0;
}