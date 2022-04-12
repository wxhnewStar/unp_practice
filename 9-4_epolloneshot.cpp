#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024

struct fds
{
    int epollfd;
    int sockfd;
};

int setnonblocking( int fd ) {
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation  = old_operation | O_NONBLOCK;
    fcntl( fd , F_SETFL, new_operation );
    return old_operation;
}

/* 将 fd 上的 EPOLLIN 和 EPOLLET 事件注册到 epollfd 指示的 epoll 内核事件表中，参数 oneshot
    指定是否注册 fd 上的 EPOLLONESHOT 事件 */
void addfd( int epollfd, int fd, bool oneshot ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if ( oneshot ) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* 重置 fd 上的事件。这样做之后，尽管 fd 上的 EPOLLONESHOT 事件被注册，但是操作系统
    仍然会出触发 EPOLLIN 事件，且只触发一次 */
void reset_oneshot( int epollfd, int fd ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLONESHOT | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

/* 工作线程 */
void* worker( void* arg ) {
    int sockfd = ( ( fds* ) arg ) -> sockfd;
    int epollfd = ( ( fds* ) arg ) -> epollfd;
    printf( "start new thread to receive data on fd: %d\n", sockfd );
    char buf[ BUFFER_SIZE ];
    memset( buf, '\0', BUFFER_SIZE );

    /* 循环读取 sockfd 上的数据，直到遇到 EAGAIN 或者 EWOULDBLOCK 错误 */
    while ( 1 ) {
        int ret = recv( sockfd, buf, BUFFER_SIZE - 1, 0 );
        if ( ret == 0 ) {
            close( sockfd );
            printf( "foreiner closed the connection\n" );
            break;
        }
        else if ( ret < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                reset_oneshot( epollfd, sockfd );
                printf( "read later\n" );
                break;
            }
        }
        else {
            printf( "get content : %s\n", buf );
            /* 休眠 5 s，模拟数据处理过程 */
            sleep( 5 );
        }
    }
    printf( "end thread receiving data on fd: %d\n",sockfd );
}


int main ( int argc, char* argv[] ) {
    if ( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int ret = 0; 
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listen >= 0 );

    ret = bind( listenfd, ( sockaddr* ) &address, sizeof( address ) );
    assert ( ret != -1 );

    ret = listen( listenfd, 5 );
    assert ( ret != -1 ); 

    struct epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    /* 注意：监听 socket 不能注册 EPOLLONESHOT 事件，否则应用程序只能处理一个客户连接！
        因为后续的客户连接请求将不再触发 listenfd 上的 EPOLLIN 事件 （如果 reset listenfd 也可以的）*/
    addfd( epollfd, listenfd, false ); 
    
    while ( 1 ) {
        int ret = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ret < 0 ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0 ; i < ret; i++ ) {
            int sockfd = events[i].data.fd;
            if ( sockfd == listenfd ) {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof( client_address );
                int connfd = accept ( sockfd, ( sockaddr* )&client_address, &client_addresslength );
                /* 对每个非监听文件描述符都注册 EPOLLONESHOT 事件 */
                addfd( epollfd, connfd, true );
                // reset_oneshot( epollfd, listenfd );
            }
            else if ( events[i].events & EPOLLIN ) {
                pthread_t worker_thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;
                /* 新启动一个工作线程为 sockfd 服务 */
                pthread_create( &worker_thread, NULL, worker, ( void* ) &fds_for_new_worker );
            }
            else {
                printf( "something else happend \n" );
            }
        }
    }
    close( listenfd );
    return 0;
}