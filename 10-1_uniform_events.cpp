#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

int setnonblocking( int fd ) {
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_operation );
    return old_operation;
}

void addfd( int epollfd, int fd ) {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;  // 以 ET 模式监听事件的可读
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* 信号处理函数 */
void sig_handler( int sig ) {
    /* 保留原来的 errno，在函数最后恢复，以保证函数的重入性 */
    int save_errno = errno;
    int msg = sig;
    printf( "now in the signal handler funciton\n" );
    sleep( 8 );
    printf( "end sleep, begin send signal to main loop\n" );
    send( pipefd[1], ( char* )&msg, 1, 0 ); /* 将信号值传入管道，以通知主循环 */
    errno = save_errno;  // 难道是保证 send 管道时的错误影响 errno？
}

/* 设置信号的处理函数 */
void addsig( int sig ){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;  // 不设置这个的话，信号中断以后原来阻塞的系统调用（这里是 epoll）不会重新启用
    sigfillset( &sa.sa_mask ); // 为什么要把所有的信号都阻塞了呢？这个 mask 的作用应该是指处理函数的时候阻塞哪些信号吧！
    assert( sigaction( sig, &sa, NULL ) != -1 );
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
    addfd( epollfd, listenfd );

    /* 使用 socketpair 创建管道，注册 pipefd[0] 上的可读事件 */
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert ( ret != -1 );

    printf( "socketpair success\n" );

    setnonblocking( pipefd[1] ); // 设置管道的写事情为 nonblocking
    addfd( epollfd, pipefd[0] ); // 监听管道的读事件 

    /* 设置一些信号的处理函数 */
    addsig( SIGHUP );
    addsig( SIGCHLD );
    addsig( SIGQUIT );
    addsig( SIGTERM );
    addsig( SIGINT );
    bool stop_server = false;

    while ( !stop_server ) {
        int number =  epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR) ) {
            printf( "epoll failure\n" );
            break;
        }

        for (int i = 0; i < number ; ++i ) {
            int sockfd = events[i].data.fd;
            /* 如果就绪的文件描述符是 listenfd,则处理新的连接 */
            if ( sockfd == listenfd ) {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof( client_address );
                int connfd = accept( listenfd, (struct sockaddr*) & client_address, & client_addresslength );
                addfd( epollfd, connfd );
                printf( "add new client: %d\n", connfd );
            }
            /* 如果就绪的文件描述符是 pipefd[0]，则处理信号 */
            else if ( (sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) { 
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if ( ret == -1 ) {
                    continue;
                }
                else if ( ret == 0 ) {
                    continue;
                }
                else {
                    /* 因为每个信号值占 1 个字节，因此按照字节来逐个接收信号。我们以 SIGTERM 为例，
                        来说明如何安全地终止服务器主循环 */
                    for ( int i = 0; i < ret; ++i ) {
                        switch ( signals[i] ) {
                            case SIGQUIT:
                            {
                                printf( "receive SIGQUIT siganl\n" );
                                continue;
                            }
                            case SIGCHLD:
                            case SIGHUP:
                            {
                                printf( "receive SIGCHLD or SIGHUP signal\n" );
                               continue;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else {
                printf( "server processing it's socket things (maybe)\n" );
            }
        }
    }

    printf( "close fds\n" );
    close( listenfd );
    close( pipefd[0] );
    close( pipefd[1] );
    return 0;
}