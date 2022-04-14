#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#define BUF_SIZE 1024

static int connfd;
/* SIGURG 信号的处理函数 */
void sig_urg( int sig ) {
    printf( "in sigurg handler function\n");
    int save_errno = errno;
    char buffer [ BUF_SIZE ];
    memset ( buffer, '\0', BUF_SIZE );
    int ret = recv( connfd, buffer, BUF_SIZE - 1, MSG_OOB );
    printf( "got %d bytes of oob data %s\n", ret, buffer );
    errno = save_errno;
}

void addsig( int sig, void (*sig_handler) (int ) ) {
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    int ret = sigaction( sig, &sa, NULL );
    assert( ret != -1 );
}

int main( int argc,char* argv[] ) {
    if ( argc <=2 ) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi( argv[2] );

    sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );

    int ret = bind( sockfd, ( sockaddr* ) &address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( sockfd, 5 );
    assert( ret != -1 );

    struct sockaddr_in client_address;
    socklen_t client_addresslength = sizeof( client_address );
    connfd = accept( sockfd, ( sockaddr* ) &client_address, &client_addresslength );
    if ( connfd < 0 ) {
        printf( "error is %s", strerror( errno ) );
    }
    else {
        addsig( SIGURG, sig_urg );
        /* 使用 SIGHUP 之前，我们必须设置 socket 的宿主进程或进程组 */
        fcntl( connfd, F_SETOWN, getpid() );

        char buffer [ BUF_SIZE ];
        while ( 1 ) {
            memset( buffer, '\0', BUF_SIZE );
            ret = recv( connfd, buffer, BUF_SIZE - 1, 0 );
            if ( ret <= 0 ) {
                break;
            }
            printf( "got %d bytes of normal data \"%s\"\n", ret , buffer );
        }

        close( connfd );
    }

    close( sockfd );
    return 0;
}