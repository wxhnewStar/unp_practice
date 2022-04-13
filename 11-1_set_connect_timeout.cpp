#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/* 超时连接函数 */
int timeout_connect( const char* ip, int port, int time ) {
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );
    /* 通过选项 SO_RCVTIMEO 和 SO_SNDTIMEO 所设置的超时时间的类型是 timeval，
        这和 select 系统调用的超时参数类型相同 */
    struct timeval timeout;
    timeout.tv_sec = time;
    timeout.tv_usec = 0;
    
    socklen_t len = sizeof ( timeout );
    ret = setsockopt( sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, len );
    assert( ret != -1 );

    ret = connect( sockfd, ( struct sockaddr* ) &address, sizeof( address ));
    if (ret == -1 ) {
        /* 连接失败了，如果是因为超时的原因错误号是 EINPROGRESS */
        if ( errno == EINPROGRESS ) {
            printf( "connecting timeout, process timeout logic \n" );
            return -1;
        }
        printf( "error occur when connecting to server: %s\n", strerror( errno ) );
        return -1;
    }
    return sockfd;
}

int main( int argc, char* argv[] ) {
    if ( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0]) );
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int time = 0;
    printf( "input the timeout value: ");
    scanf("%d", &time );

    int sockfd = timeout_connect( ip, port, time );
    if ( sockfd < 0 ) {
        return 1;
    }
    printf( "connection build without timeout!\n" );
    return 0;
}