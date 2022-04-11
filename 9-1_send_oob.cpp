#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
using namespace std;

#define BUF_SIZE 1024

int main (int argc ,char *argv[]) {
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

    // 开始连接
    if ( connect( sockfd, (struct sockaddr*)&server_address ,sizeof( server_address) ) < 0) {
        printf ( "connection failed \n");
    }
    else {
        int output_mode; // 0 代表普通，1 代表oob
        char buffer[ BUF_SIZE ];
        while ( 1 ) {

            memset( buffer, '\0', BUF_SIZE);
            printf("Please input the mode you want to send, 0 presents normal data, 1 presents oob data\n");
            scanf("%d", &output_mode ); 
            printf("Please input the data you want to send:\n");
            scanf("%s", buffer);

            switch ( output_mode )
            {
            case 0:
                send(sockfd, buffer, strlen(buffer), 0 );
                break;
            case 1:        
                send(sockfd, buffer, strlen(buffer), MSG_OOB );
                break;
            default:
                break;
            }
        }
    }

    close (sockfd);
    return 0;
}