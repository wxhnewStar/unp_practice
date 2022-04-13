#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <poll.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#define USER_LIMIT 5 // 最大用户数量
#define BUFFER_SIZE 64 // 读缓冲区的大小
#define FD_LIMIT 65535 // 文件描述符数量限制

/* 客户数据： 客户端 socket 地址、 待写到客户端数据的位置、从客户端读入的数据 */
struct client_data
{
    sockaddr_in address;
    char* write_buf;
    char buf[ BUFFER_SIZE ];
};

int setnonblocking( int fd ) {
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_operation );
    return old_operation;
}

int main( int argc, char* argv[] ) {
    if ( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename ( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    sockaddr_in address;
    bzero( &address, sizeof ( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    inet_pton( AF_INET, ip, &address.sin_addr );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert ( listenfd >= 0 );

    int ret = 0;
    ret = bind( listenfd, ( sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, USER_LIMIT ); 
    assert( ret != -1 );

    /* 创建 users 数组，分配 FD_LIMIT 个 client_data 对象。可以预期： 每个可能的 socket 连接都
        可以获得一个这样的对象，并且 socket 的值可以直接用来索引 （作为数组的下标） socket 连接
        对应的 client_data 对象，这是将 socket 和客户数据关联的简单而高效的方式。 */
    client_data* users = new client_data[ FD_LIMIT ];
    /* 尽管我们分配了足够多的 client_data 对象，但为了 poll 的性能，仍然有必要限制用户的数量 */
    pollfd pollfds[ USER_LIMIT + 1 ]; // 多出的一个是 listenfd
    int user_counter = 0;
    for ( int  i = 1; i <= user_counter; ++i ) {
        pollfds[i].fd = -1;
        pollfds[i].events = 0;
        pollfds[i].revents = 0; // wxh 自己加的
    }
    pollfds[0].fd = listenfd;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;

    while ( 1 ) {
        ret = poll( pollfds, user_counter + 1, -1 );
        if ( ret < 0 ) {
            printf ( "poll failure\n" );
            break; // 唯一死循环出口
        }

        for ( int i = 0 ; i < user_counter + 1; ++i ) {
            if ( ( pollfds[i] .fd == listenfd ) && ( pollfds[i].revents & POLLIN ) ) {
                struct  sockaddr_in client_address;
                socklen_t client_addresslength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* ) &client_address, &client_addresslength );
                if ( connfd < 0 ) {
                    printf( "error is: %s", strerror( errno ) );
                    continue;
                }
                /* 如果请求太多，则关闭新到的连接 */
                if ( user_counter >= USER_LIMIT ) {
                    const char* info = "too many users\n";
                    printf ( "%s", info );
                    send( connfd, info ,strlen( info ), 0 );
                    close( connfd );
                    continue;
                }
                /* 对于新的连接， 同时修改 fds 和 users 数组，前文已经提到， users[connfd] 对应于
                    新连接文件描述符 connfd 的客户数据 */
                ++user_counter;
                users[ connfd ].address = client_address;
                setnonblocking( connfd );
                pollfds[user_counter].fd = connfd;
                pollfds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                pollfds[user_counter].revents = 0;
                printf( "comes a new user, now have %d users\n", user_counter );
            }
            else if ( pollfds[i].revents & POLLERR ) {
                printf( "get an error from %d\n", pollfds[i].fd );
                char errors[100];
                memset( errors, '\0', 100 );
                socklen_t error_length = sizeof( errors );
                if ( getsockopt( pollfds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &error_length ) < 0 ) {
                    printf( "get socket option failed\n" );
                    continue;
                }
            }
            else if ( pollfds[i].revents & POLLRDHUP ) {
                /* 如果客户端关闭连接，则服务器也关闭对应的连接，并将用户总数 -1 */
                users[ pollfds[i].fd ] = users[ pollfds[user_counter].fd ]; // 将排在后面的 user 挪到被删除 user 的位置
                close( pollfds[i].fd );
                pollfds[i] = pollfds[user_counter];
                i--; // 注意 i 也要减，因为当前的值已经被取代了
                user_counter--;
                printf( "a client left\n" );
            }
            else if ( pollfds[i].revents & POLLIN ) {
                int connfd = pollfds[i].fd;
                memset( users[connfd].buf, '\0', BUFFER_SIZE );
                ret = recv( connfd, users[connfd].buf, BUFFER_SIZE - 1, 0 );
                printf( "get %d bytes of client data \"%s\" from %d\n", ret , users[connfd].buf, connfd );
                if (ret < 0 ) {
                    /* 如果读操作出错，则关闭连接 */
                    if ( errno != EAGAIN || errno != EWOULDBLOCK ) {
                        close( connfd );
                        users[ pollfds[i].fd ] = users[ pollfds[user_counter].fd ];
                        pollfds[i] = pollfds[user_counter];
                        i--; // 注意 i 也要减，因为当前的值已经被取代了
                        user_counter--;
                    }
                }
                else if ( ret == 0 ) {
                    // 空的? 可能因为前面 POLLRDHUP 处理了这种情况
                }
                else {
                    /* 如果收到了客户数据，则通知其他 socket 连接准备写数据 */
                    for ( int j = 1; j <= user_counter; ++j ) {
                        if ( pollfds[j].fd == connfd )
                            continue;
                        
                        pollfds[i].events |=  ~POLLIN;
                        pollfds[j] .events != POLLOUT;
                        users[pollfds[j].fd].write_buf = users[connfd].buf; // 要发送的 socket 中发送数据指向 connfd 中的 buf
                    }
                }
            }
            else if ( pollfds[i].revents & POLLOUT ) {
                int connfd = pollfds[i].fd;
                if ( !users[connfd].write_buf ) {
                    continue;
                }
                ret = send( connfd, users[connfd].write_buf, strlen( users[connfd].write_buf ), 0 );
                users[connfd].write_buf = NULL;
                /* 写完数据以后需要重新注册 pollfds[i] 上的可读事件 */
                pollfds[i].events |= ~POLLOUT;
                pollfds[i].events |= POLLIN;
            }
        }
    }

    delete []users;
    close ( listenfd );
    return 0; 
}
