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

/* 存在问题： close 掉连接以后，好像没有删除 epoll 中对应的描述符！ */

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

/* 设置文件描述符为非阻塞的 */
int setnonblocking( int fd ) {
    int old_option = fcntl( fd, F_GETFL );
    old_option |= O_NONBLOCK;
    fcntl( fd, F_SETFL, old_option );
    return old_option;
}

/* 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中，
参数 enable_et 指定是否对 fd 启用 EF 模式 */
void addfd( int epollfd, int fd, bool enable_et ) {
    epoll_event event;
    event.data.fd =  fd;
    event.events = EPOLLIN;
    if ( enable_et ) {
        event.events |= EPOLLET;
    }
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd ); // 最后设置为 非阻塞 的，因为到时候会自己来通知主进程
}

/* LT 模式的工作流程 */
void lt( epoll_event* events, int number, int epollfd, int listenfd ) {
    char buf[BUFFER_SIZE];
    for ( int i = 0; i < number; ++i ) {
        int sockfd = events[i].data.fd;
        if ( sockfd == listenfd ) {
            struct sockaddr_in client_addrss;
            socklen_t client_addrsslength = sizeof( client_addrss );
            int connfd = accept( listenfd, ( sockaddr* )&client_addrss, &client_addrsslength );
            addfd( epollfd, connfd, false ); // 对 connfd 禁用 ET 模式
        }
        else if ( events[i].events & EPOLLIN ) {
            /* 只要 socket 读缓存中还有未读出的数据，这段代码就会被触发 */
            printf( "event trigger once\n" );
            memset( buf, '\0', BUFFER_SIZE );
            int ret = recv( sockfd, buf, BUFFER_SIZE - 1, 0 );
            if ( ret <= 0 ) {
                printf( "connfd: %d is close.", sockfd );
                close ( sockfd );
                continue;
            }
            printf ( "get %d bytes of content: %s\n", ret, buf );
        } else {
            printf( "something going wrong\n");
        }
    }
}

/* ET 模式的工作流程 */ 
void et( epoll_event* events, int number, int epollfd, int listenfd ) {
    char buf[ BUFFER_SIZE ];
    for ( int i = 0; i < number; i++) {
        int sockfd = events[i].data.fd;
        if ( sockfd == listenfd ) {
            struct sockaddr_in client_address;
            socklen_t client_addresslength = sizeof ( client_address );
            int connfd = accept( listenfd, ( sockaddr* ) &client_address, &client_addresslength );
            addfd( epollfd, connfd, true );
        }
        else if ( events[i].events & EPOLLIN ) {
            /* 这段代码不会被重复触发，所以我们循环读取数据，以确保把 socket 读缓存中的所有数据读出 */
            printf( "event trigger once\n" );
            while ( 1 ) {
                memset( buf, '\0', BUFFER_SIZE );
                int ret = recv( sockfd, buf, BUFFER_SIZE - 1, 0 );
                if ( ret < 0 ) {
                    /* 由于是非阻塞 IO，因此下面的条件成立表示数据已经全部读取完毕。
                        此后，epoll 就能再次触发 sockfd 上的 EPOLLIN 事件，以驱动下一次读操作 */
                    if ( (errno == EAGAIN ) || ( errno == EWOULDBLOCK ) ) {
                        printf( "read later\n" );
                        break;
                    }
                    // 如果不是上面两种表示非阻塞内容读取完的错误，说明出现了其他未预知的问题，关闭连接
                    printf( "something strange wrong happen on this connection: %d\n", sockfd );
                    close ( sockfd );
                    break;
                }
                else if ( ret == 0 ) {
                    printf( "connfd: %d is close.\n", sockfd );
                    close ( sockfd );
                    break;
                    // 不用退出？
                }
                else {
                    printf( "get %d bytes of content: %s\n", ret, buf );
                }
            }
        }
        else {
            printf( "something else happened \n");
        }
    }
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
    addfd( epollfd, listenfd, true ); // 服务器监听采用的是 ET 模式

    int mode; 
    printf ( "input which mode you want to choose, 0 presents lt, 1 presents et.\n");
    scanf( "%d", &mode );
    
    while ( 1 ) {
        int ret = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ret < 0 ) {
            printf( "epoll failure\n" );
            break;
        }

        switch ( mode )
        {
        case 0:    
            lt( events, ret, epollfd, listenfd );
            break;
        case 1:
            et( events, ret, epollfd, listenfd);
            break;
        default:
            printf("Invalid input mode!\n");
            break;
        }
    }

    close( listenfd );
    return 0;
}