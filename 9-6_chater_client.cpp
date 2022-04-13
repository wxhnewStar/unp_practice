#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

int main ( int argc, char* argv[] ) {
     if ( argc <= 2 ) {
        printf("usage: %s ip_address port_number \n",   argv[0] );
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi( argv[2] );

    struct sockaddr_in server_address;
    bzero( &server_address, sizeof( server_address) );
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_address.sin_addr);
    
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert( sockfd >= 0);
    if ( connect( sockfd, (struct sockaddr* ) &server_address, sizeof( server_address ) ) < 0 ) {
        printf( "connection failed\n" );
        close( sockfd );
        return 1;
    } 

    pollfd fds[2];
    /* 注册 标准输入 和 sockfd 上的可读事件 */
    fds[0].fd = 0; fds[0].events = POLLIN; fds[0].revents = 0;
    fds[1].fd = sockfd; fds[1].events = POLLIN | POLLRDHUP ; fds[1].revents = 0;

    char read_buf[ BUFFER_SIZE ];
    int pipefd[2];
    int ret = pipe( pipefd );
    assert( ret !=-1 );

    while ( 1 ) {
        ret = poll( fds, 2, -1 );
        if ( ret < 0 ) {
            printf( "poll failure\n" );
            break;
        }
        
        if ( fds[1].revents & POLLRDHUP ) {
            printf( " server close the connection\n" );
            break;
        }
        else if ( fds[1].revents & POLLIN ) {
            memset( read_buf, '\0', BUFFER_SIZE );
            recv( fds[1].fd, read_buf, BUFFER_SIZE - 1, 0 );
            printf ("%s\n", read_buf );   //接收到的信息就仅仅是单独输出罢了，也无法保证是否完整的一句话，也没有发送者信息
        }

        if ( fds[0].revents & POLLIN ) {
            /* 使用 splice 将用户输入的数据直接写到 sockfd 上 */
            ret = splice( 0, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );

            ret = splice( pipefd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );
        }
    }

    close( sockfd );
    return 0;
}