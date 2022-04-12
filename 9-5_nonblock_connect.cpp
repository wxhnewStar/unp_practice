#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/select.h>

#define BUFFER_SIZE 1023

int setnonblocking( int fd ) {
    int old_operation = fcntl ( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd , F_SETFL, new_operation );
    return old_operation;
}

/* 超时连接函数，参数分别是服务器 ip 地址，端口号和超时时间（单位：毫秒）。
    函数成功时返回已经处于连接状态的 socket，失败则返回 -1 。*/
int unblock_connect( const char* ip, int port, int time ) {
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );
    int fdopt = setnonblocking( sockfd );  // 设置 客户端 socket 为 nonblocking，保存原有的状态

    ret = connect( sockfd, ( sockaddr* ) &address, sizeof( address ) );
    if ( ret == 0 ) {
        /* 连接立即成功，则恢复 sockfd 的属性，并立即返回之 */
        printf( "connection immediately finish with the socket: %d\n", sockfd );
        fcntl( sockfd, F_SETFL, fdopt );
        return sockfd;
    }
    else if ( errno != EINPROGRESS ) {
        /* 如果连接没有立即建立，那么只有当 errno 是 EINPRORESS 时才表示连接还在进行，否则出错返回 */
        printf ( "unblock connect not support\n" );
        return -1;
    }
    
    fd_set writefds;
    struct timeval timeout;

    FD_ZERO( &writefds );
    FD_SET ( sockfd, &writefds );

    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    ret = select( sockfd + 1, NULL, &writefds ,NULL, &timeout );
    if ( ret <= 0 ) {
        /* select 超时或出错，立即返回 */
        printf( "connection time out \n" );
        close( sockfd );
        return -1;
    }

    if ( !FD_ISSET( sockfd, &writefds ) ) {
        printf( "no events on sockfd found\n" );
        close ( sockfd );
        return -1;
    }

    int error = 0;
    socklen_t length = sizeof( error );  // 注意 getsockopt 的 length 类型是 socklen_t( unsigned int )
    
    /* 调用 getsockopt 来获取并清楚 sockfd 上的错误 */
    if( getsockopt( sockfd, SOL_SOCKET, SO_ERROR, &error, &length ) < 0 ) {
        printf( "get socket option failed\n" );
        close( sockfd );
        return -1;
    }

    /* 错误号不为 0 表示连接出错 */
    if ( error != 0 ) {
        printf( "connection failed after select with the error: %d\n", error );
        printf( "it's meaning: %s\n",strerror (error) );
        close( sockfd );
        return -1;
    }

    /* 连接成功 */
    printf( "connection ready after select with the socket: %d\n", sockfd );
    fcntl( sockfd, F_SETFL, fdopt );
    return sockfd;
}


int main ( int argc, char* argv[] ) {
    if ( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    /* 等待 10 秒来连接 */
    int sockfd = unblock_connect( ip, port, 10 );
    if ( sockfd < 0 ) {
        return 1;
    }
    close( sockfd );
    return 0;
}