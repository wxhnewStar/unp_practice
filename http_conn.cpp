#include "http_conn.h"
#include <sys/uio.h>

/* 定义 HTTP 相应的一些状态信息 */
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

/* 网站的根目录 */
const char* doc_root = "/var/www/html";

int setnonblocking( itn fd )
{
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_operation );
    return old_operation;
}

void addfd( int epollfd, int fd ,bool one_shot )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // EPOLLRDHUP 在2.6.17 以后的内核版本中才有
    if ( one_shot )
        event.events |= EPOLLONESHOT;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd ,&event );
    setnonblocking( fd );
}

void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd ); // 负责关闭文件描述符，并从内核事件表中移除该 fd 
}

boid modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET |  EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;


void http_conn::close_conn( bool real_close )
{
    if ( real_close && ( m_sockfd != -1 ) )
    {
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;  /* 关闭一个连接时，将客户总量减 1 */
    }
}

void http_conn::init( int sockfd , const sockaddr_in& addr )
{
    m_sockfd = sockfd;
    m_address = addr;
    /* 如下两行是为了避免 TIME_WAIT 状态，仅用于调试，实际使用时应该去掉 */
    // int reuse = 1;
    // setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, m_sockfd, true );  // EPOLLONESHOT 需要每次都重新注册，主要是为了线程安全
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset( m_read_buf,' \0', READ_BUFFER_SIZE );
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
    memset( m_real_file, '\0', FILENAME_LEN );
}


/* 从状态机：其分析请参考 8-3 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
    {
        temp = m_read_buf[ m_checked_idx ];
        if( temp == '\r' )
        {
            if ( ( m_checked_idx + 1 ) == m_read_idx )
            {
                return LINE_OPEN;
            }
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' )
            {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if( temp == \'\n' )
        {
            if ( (m_checked_idx > 1 ) && ( m_read_buf[m_checked_idx - 1] == '\r' ) )
            {
                m_read_buf[ m_checked_idx - 1] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或者对方关闭连接 */
bool http_conn::read()
{
    if ( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }
    
    int bytes_read = 0;
    while ( true )
    {
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if ( bytes_read == 1 )
        {
            if ( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return false;
        }
        else if ( bytes_read == 0 )
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/* 解析 HTTP 请求行，获得请求方法、目标 URL，以及 HTTP 版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
    m_url = strpbrk( text, " \t" );
    if ( !m_url )
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* mtehod = text;
    if ( strcasecmp( mtehod, "GET" ) == 0 )
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST; // 其他的方法不支持 
    }

    m_url += strspn( m_url, " \t" );
    m_version =  strpbrk( m_url, " \t" );
    if ( !m_version )
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn( m_version, " \t" );
    if ( strcasecmp( m_version, "HTTP/1.1") != 0 )
    {
        return BAD_REQUEST;
    }

    if ( strncasecmp( m_url, " http://", 7) == 0 )
    {
        m_url += 7;
        m_url = strchr( m_url, '/');
    }

    if ( !m_url || m_url[0] != '/' )
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* 解析 HTTP 请求的一个头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers( char* text )
{
    /* 遇到空行，表示头部字段解析完毕 */
    if ( text[0] == '\0' )
    {
        /* 如果 HTTP 请求有消息体，则还需要读取 m_content_length 字节的消息体，状态机转移到 check_state_content 状态 */
        if ( m_content_length != 0 )
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        /* 否则说明我们已经得到了一个完整的 HTTP 请求 */
        return GET_REQUSET;
    }
    /* 处理 Connection 头部字段 */
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 )
        {
            m_linger = true;
        } 
    }
    /* 处理 Content-Length 头部字段 */
    else if( strncasecmp( text, "Content-Length:" , 15 ) == 0 )
    {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol( text );
    }
    /* 处理 Host 头部字段 */
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else 
    {
        printf( "opp! unknow header %s\n", text );
    }

    return NO_REQUEST;
}


/* 我们没有真正解析 HTTP 请求的消息体，只是判断它是否被完整地写入了 */
http_conn::HTTP_CODE http_conn::parse_content( char* text )
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUSET;
    }
    return NO_REQUEST; 
}

/* 主状态机 */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    
    while( ( ( m_check_state == CHECK_STATE_CONTENT) && ( line_status == LINE_OK) ) ||
        ( ( line_status == parse_line() ) == LINE_OK ) )
    {
        text = get_line();  // 从自己包装好的函数里读取请求里的行
        m_start_line = m_checked_idx;
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state )
        {
            case: CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case: CHECK_STATE_HEADER:
            {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUSET )
                {
                    return do_request(); // 发现是 get 请求的话，直接执行请求了
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content( text );
                if ( ret == GET_REQUSET )
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                {
                    return INTERNAL_ERRNR;
                    break;
                }
        }
    }

    return NO_REQUEST;
}

/* 当得到一个完整、正确的 HTTP 请求时，我们就分析目标文件的属性。如果目标文件存在、对所有用户可读，且不是目录，则使用 mmap 将其映到
地址 m_file_addrsess 处，并告诉调用者获取文件成功 */
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );

    if ( stat( m_real_file, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;
    }

    if ( !( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDEDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;
    }

    int fd = open( m_real_file, O_RDONLY );
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close ( fd );
    return FILE_REQUEST;
}

/* 对内存映射区执行 munmap 操作 */
void http_conn::unmap()
{
    if ( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

/* 写 HTTP 响应， 返回 false 则代表出错或者写完断开连接， true 表示服务器继续监听当前 sockfd */
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if ( bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );  // 发送完了，那么我们直接准备接收下一个请求
        init();  // 重置跟一次连接有关的信息
        return true;
    }

    while ( 1 )
    {
        temp = writev( m_sockfd , m_iv, m_iv_count );
        if ( temp <= -1 )
        {
            /* 如果 TCP 写缓冲没有空间，则等待下一轮 EPOLLIN 事件。虽然在此期间，服务器无法接收到同一客户的
            下一个请求，但这可以保证连接的完整性 */
            if ( errno == EAGAIN )
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send )
        {
            /* 发送 HTTP 响应成功，根据 HTTP 请求中的 connection 字段决定是否立即关闭连接 */
            unmap();
            if( m_linger )
            {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            }
        }
    }
}

/* 往写缓冲中写入待发送的数据 */
bool http_conn::add_response( const char* format, ...)
{
    if ( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false; // 如果要发送的值大于缓冲区大小，直接返回错误
    }

    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list );
    if ( len >= ( WRITE_BUFFER_SIZE -1 - m_write_idx ) ) // 大于说明缓冲区大小不够！
    {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title )
{
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status , title);
}

bool http_conn::add_headers( int content_len )
{
    if ( !add_content_length( content_len )  )  // 自己加的
        return false;
    if ( !add_linger() )
        return false;
    if ( !add_blank_line() )
        return false;
    return true;
}

bool http_conn::add_content_length( int content_len )
{
    return add_response("Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", (m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

/* 根据服务器处理 HTTP 请求，决定返回给客户端的内容 */
bool http_conn::process_write( HTTP_CODE ret )
{
    switch ( ret )
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( !add_content( error_500_form ) )
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if( !add_content( error_400_form ) )
            {
                return false;
                break;
            }
        }
        case NO_REQUEST:
        {
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( !add_content( error_404_form ) )
            {
                return false;
            }
            break;
        }
        case FORBIDEDEN_REQUEST:
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( !add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line( 200, error_200_title );
            if ( m_file_stat.st_size != 0 ) // 有文件要传输回去的话，不在这里传输，只是做好设置？
            {
                add_headers( m_file_stat.st_size );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;  
            }
            else 
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( !add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0]_iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
} 

/* 由线程池中的工作线程调用，这是处理 HTTP 请求的入口函数 */
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST )
    {
        modfd( m_epollfd , m_sockfd , EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret );
    if ( !write_ret )
    {
        close_conn();
    }

    modfd( m_epollfd, m_sockfd, EPOLLOUT ); // 通知可以写，由主处理逻辑去进行写操作
    
}