#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }

    // 从参数中获取 ip 和 port
    const char* ip = argv[1];
    int port = atoi( argv [2]);
    
    // 设置好 socket 地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton( AF_INET, ip, &address.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert (sock >= 0); // 失败则返回 -1，并设置 errno

    int ret = bind(sock, (struct sockaddr*) &address, sizeof(address) );
    assert(ret != -1);

    ret = listen( sock, 5 ); // 第二参数 backlog 为 accept 队列的最大值
    assert( ret != -1 );

    // 暂停 20 秒以等待客户端连接和先关操作 完成
    sleep ( 5 );
    
    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof ( client );
    int connfd = accept (sock ,(struct sockaddr*) &client, &client_addrlength);
    if (connfd < 0 ) {
        printf(" errno is : %d\n", errno);
        strerror(errno);
    }
    else {
         // 接收连接成功则打印出客户端的 ip 地址和端口号
         char remote[INET_ADDRSTRLEN];
         printf ( "connected with ip: %s and port : %d \n", inet_ntop( AF_INET, &client.sin_addr,remote,INET_ADDRSTRLEN),
                ntohs(client.sin_port)); // port 为 16位数，一共 65536 个端口  
        close (connfd);
    }

    close ( sock );
    return 0;
}
