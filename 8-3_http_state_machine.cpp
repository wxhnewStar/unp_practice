#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#define BUFFER_SIZE 4096;  // 缓冲区大小

enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER };

enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

/* 服务器处理 HTTP 请求的结果： NO_REQUEST 表示请求不完整，需要继续读取客户端数据
    GET_REQUEST 表示获得了一个完整的客户端请求； BAD_REQUEST 表示客户请求有语法错误；
    FORBIDDEN_REQUEST 表示客户端对资源没有足够的访问权限；
    INTERNAL_ERROR 表示服务器内部错误； CLOSED_CONNETCTION 表示客户端已经关闭连接了
    */
enum HTTP_CODE { NO_REQUSET, GET_REQUEST, BAD_REQUEST,
     FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNETCTION };

static const char* szret[] = {"I get a correct result \n", "Something wrong\n"};

/* 从状态机， 用于解析出一行内容
   checked_index 指向 buffer 中当前正在分析的字节， read_index 指向 buffer 中客户数据尾部的下一个字节 */
enum LINE_STATUS parse_line ( char* buffer, int& checked_index, int& read_index ) {
    char temp;
    for( ; checked_index < read_index; ++checked_index ) {
        // 获取当前要分析的字节
        temp = buffer[ checked_index ];

        if ( temp == '\r' ) { // 回车符，表示读取到一个完整的行
            /* 如果该字符碰巧是目前 buffer 中的最后一个已经被读入的客户数据，那么这次分析没有读取到
                一个完整的行， 返回 LINE_OK 以表示还需要继续读取客户数据才能进一步分析 */
            if ( ( checked_index + 1 ) == read_index ) {
                return LINE_OPEN;
            }
            else if ( buffer[ checked_index + 1 ] == '\n' ) {
                buffer [ checked_index++ ] = '\0';
                buffer [ checked_index++ ] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
        else if ( temp == '\n' ) { // 当前是换行符,有可能上次刚好从 socket 读到 \r 
            if ( ( checked_index > 1 ) && buffer[checked_index - 1] == '\r' ) {
                buffer [ checked_index -1 ] = '\0';
                buffer [ checked_index++ ] = '\0';
                return LINE_OK; 
            }
        }
        return LINE_BAD;
    }

    /* 如果所有内容都分析完毕业没有遇到 '\r' 字符， 则返回 LINE_OPEN,
        表明还要继续读取才能进一步分析 */
    return LINE_OPEN;
}


/* 分析请求行 */
enum HTTP_CODE parse_requestline ( char* temp, enum CHECK_STATE& checkstate ) {
    char* url = strpbrk( temp, " \t" ); // 找到第一个 空格 或者 换行符
    
    if ( !url ) {
        return BAD_REQUEST;
    }
    *url++ = '\0'; // 找到方法后的第一个空格或者换行符的位置，通过这个方法隔断 方法 和 url

    char* method  = temp;
    if (strcasecmp( method, "GET" ) == 0 ) { // 仅支持 GET 方法。函数是用于不区分大小写比较是否相等
        printf( "The request method is GET\n" );
    }
    else {
        return BAD_REQUEST;  // 其他方法暂时不支持，直接拜拜
    }

    url += strspn( url, " \t" ); // 方法 和 url 前面这一段还有几个空格和换行符，跳过它们，到达 url 的位置
    char* version = strpbrk( url, " \t" );
    if ( !version ) {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn( version, " \t" );

    if (strcasecmp( version, "HTTP/1.1" ) != 0 ) { // 仅支持 1.1 版本的 HTTP 协议
            return BAD_REQUEST;
    }
    
    /* 检查 url 是否合法 */
    if ( strncasecmp ( url, "http://", 7) == 0 ) {
        url += 7;
        url =  strchr( url, '/' ); // 假如有 http 这个前缀，直接跳过
    }

    if ( !url || url[0] != '/' ) {
        return BAD_REQUEST;
    }

    printf( "The request URL is: %s\n", url );
    /* HTTP 请求处理完毕，状态转移到头部字段的分析 */
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUSET;
}

/* 分析头部字段 */
enum HTTP_CODE parse_headers( char* temp ) {
    // 遇到一个空行，说明我们得到了一个正确的 HTTP 请求
    if ( temp[ 0 ] == '\0' ) {
        return GET_REQUEST;
    }
    else if ( strncasecmp( temp, "Host:",5) == 0) {
        temp += 5;
        temp += strspn( temp, " \t" );
        printf( "the request host is: %s\n", temp );
    }
    else {
        printf( "I can not handle this header\n" );
    }
    return NO_REQUSET;
}

/* 分析 HTTP 请求的入口函数 */
enum HTTP_CODE parse_content( char* buffer, int& checked_index, CHECK_STATE& checkstate,
                        int& read_index, int& start_line ) {
    LINE_STATUS linestatus = LINE_OK;
    HTTP_CODE retcode = NO_REQUSET;

    // 主状态机，从 buffer 中取出所有完整的行
    while ( (linestatus = parse_line( buffer, checked_index, read_index ) ) == LINE_OK ) {
        char* temp = buffer + start_line;
        start_line = checked_index;
        /* checkstate 记录主状态机当前的状态 */
        switch ( checkstate )
        {
        case CHECK_STATE_REQUESTLINE:
            retcode = parse_requestline( temp, checkstate );
            if ( retcode == BAD_REQUEST ){
                return BAD_REQUEST;
            }
            break;
        case CHECK_STATE_HEADER:
            retcode = parse_headers( temp );
            if ( retcode == BAD_REQUEST ) {
                return BAD_REQUEST;
            }
            else if ( retcode == GET_REQUEST ) {
                return GET_REQUEST;
            }
            break;
        default:
            return INTERNAL_ERROR;
        }
    }

    /* 若没有读取到一个完整的行，则表示还需要继续读取客户数据才能进一步分析 */
    if ( linestatus == LINE_OPEN ) {
        return NO_REQUSET;
    }
    else {
        return BAD_REQUEST;
    }
}

int main( int argc, const char* argv[] ) {
    if ( argc < = 2 ) {
        printf( "usage: %s ip_address port_nuber\n", basename( argv[0] ) );
    }

    const char* ip = argv[1]; 
    int port = atoi( argv[2] );

    sockaddr_in server_addr;
    bzero( &server_addr, sizeof( server_addr ) );
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons( port );
    inet_pton( AF_INET, ip, &server_addr.sin_addr );

    int listen_fd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listen_fd >= 0 );
    int ret = bind( listen_fd, (sockaddr*) server_addr, sizeof( server_addr ) );
    assert( ret != -1 );
    ret = listen( listen_fd, 5 );
    assert( ret != 1 );
    
    sockaddr_in client_addr;
    socklen_t client_addrlength = sizeof( client_addr );
    
    int conn_fd = accept( listen_fd, &client_addr, &client_addrlength );
    if ( conn_fd < 0 ) {
        printf( "errno is: %d\n", errno );
    }
    else {
        char buffer [ BUFFER_SIZE ];
        memset( buffer, '\0', BUFFER_SIZE );
        int data_read = 0;
        int read_index = 0;     // 当前已经读取了多少字节的客户数据
        int checked_index = 0;  // 当前已经分析了多少自己的客户数据
        int start_line = 0;     // 行在 buffer 中的起始数据

        /* 设置主状态机的初始状态 */
        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
        while (1) {
            data_read = recv( connfd, buffer, BUFFER_SIZE, 0 );
            if ( data_read == -1 ) {
                printf ( "reading failed\n" );
                break;
            }
            else if ( data_read == 0 ) { // 读取到了 EOF
                printf( "remote clinet has closed the connection\n" );
                break;
            }
            
            read_index += data_read;
            HTTP_CODE result = parse_content( buffer, checked_index, checkstate, read_index, start_line );
            
            if ( result == NO_REQUSET )
                continue;
            else if ( result == GET_REQUEST ) { // 得到了一个完整、正确的 HTTP 请求
                send( conn_fd, szret[0], strlen( szret[0] ), 0 );
                break;
            } else {
                send( conn_fd, szret[1], strlen( szret[1] ), 0 );
                break;
            }
        }
        close( conn_fd );
    }
    close( listen_fd );
    return 0;

    /*enum CHECK_STATE cur = CHECK_STATE_HEADER;
    const char* testString =  "GET /home.html  HTTP/1.1";
    char* test = (char*) malloc(strlen(testString) +1);
    strcpy(test,testString);
    printf ("%s\n",test);
    if ( parese_requestline( test,  cur ) == BAD_REQUEST ) {
        printf ( "opp, bad request!\n");
    }
    free((void*)test);
    return 0; */
}