#ifndef PROCESSPOLL_H
#define PROCESSPOLL_H

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
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* 描述一个子进程的类，m_pid是目标子进程的 PID，m_pipefd 是父进程和子进程通信用的管道 */
class process {
public:
    process(): m_pid(-1) {}
public:
    pid_t m_pid;
    int m_pipefd[2];
};

/* 进程池类，将它定义为模板类是为了代码复用。其模板参数是处理逻辑任务的类 */
template<typename T>
class processpoll {
private:
    /* 将构造函数定义为私有的，因此我们只能通过后面的 create 静态函数来创建 processpoll 实例 */
    processpoll( int listenfd, int process_number = 8 );

public:
    /* 单例模式，以保证程序最多创建一个 processpoll 实例，这是程序正确处理信号的必要条件 */
    static processpoll< T >* create( int listenfd, int process_number = 8 ) {
        if ( !m_instance ) {
            m_instance = new processpoll<T>( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpoll() {
        delete [] m_sub_process;
    }

    /* 启动进程池 */
    void run();

public:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    static const int MAX_PROCESS_NUMBER = 16; // 进程池允许的最大子进程数量
    static const int USER_PER_PROCESS = 65536; // 每个子进程最多能处理的客户数量
    static const int MAX_EVENT_NUMBER = 10000; // epoll 最多能够处理的事件数
    int m_process_number; // 进程池中进程总数
    int m_idx; // 子进程在池中的序号，从 0 开始
    int m_epollfd; // 每个进程都有一个 epoll 内核时间表，用 m_pollfd 标识
    int m_listenfd; // 监听 socket
    int m_stop; // 子进程通过 m_stop 来决定是否停止运行
    process* m_sub_process; // 保存所有子进程的描述信息
    static processpoll<T>* m_instance;
};

template< typename T>
processpoll< T >* processpoll<T>::m_instance = NULL;

/* 用于处理信号的管道，用以实现统一事件源，后面称之为信号管道 */
static int sig_pipefd [2];


static int setnonblocking( int fd ) {  
    int old_operation = fcntl( fd, F_GETFL );
    int new_operation = old_operation | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_operation );
    return old_operation;    
}

static void addfd( int epollfd , int fd ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET; // 以 ET 方式监听读事件
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* 从 epollfd 标识的 epollfd 内核事件表中删除 fd 上的所有注册事件 */
static void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd , 0 );
    close ( fd ); //同时负责把该 文件描述符 关闭
}

static void sig_handler( int sig ) {
    int save_errno = errno;
    int msg = sig;
    send( sig_pipefd[1], (char*) *msg, 1 , 0 );
    errno = save_errno;
}

static void addsig( int sig, void ( handler)  (int ), bool restart = true ) {
    struct sigaction sa;
    memset( &sigaction, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if ( restart ) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

/* 进程池构造函数。 参数 listenfd 是监听 socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。
    参数 process_number 指定进程池中子进程的数量 */
template< typename T>
processpoll<T>::processpoll( int listenfd, int process_number = 8 )
        :m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ),m_stop(false) {

    assert( (process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );
    m_sub_process = new process[ process_number];
    assert( m_sub_process );

    /* 创建 process_number 个子进程，并建立它们和父进程之间的管道 */
    for ( int i = 0; i< process_number; ++i ) {
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0 , m_sub_process->m_pipefd );
        assert( ret == 0 );

        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if ( m_sub_process[i].m_pid > 0 ) {
            /* 父进程 */
            close( m_sub_process[i].m_pipefd[1] ); // 父进程读取子进程的信号，因此关闭写管道
            continue;
        }
        else {
            close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}


/* 统一事件源 */
template< typename T>
void processpoll<T>::setup_sig_pipe() {
    /* 创建 epoll 事件监听表和信号通道 */
    m_epollfd = epoll_create( 5 );
    assert( m_epollfd != -1 );

    /* 定义子进程的 信号 传递管道 */
    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0 , sig_pipefd );
    assert ( ret != -1 );

    setnonblocking( sig_pipefd[1] );
    addfd( m_epollfd, sig_pipefd[0] );
    
    /* 设置信号处理函数 */
    addsig( SIGCHLD, sig_handler );
    addsig( SIGTERM, sig_handler );
    addsig( SIGINT, sig_handler );
    addsig( SIGPIPE, SIG_IGN );
}

/* 父进程中 m_idx 值为 -1，子进程中 m_idx 值大于 0
    ，我们据此判断接下来要运行的是父进程代码还是子进程 */
template< typename T>
void processpoll<T>::run() {
    if ( m_idx != -1 ) {
        run_child();
        return;
    }
    else
        run_parent();
}

template< typename T>
void processpoll<T>::run_child() {
    setup_sig_pipe();

    /* 每个子进程都通过其在进程中的序号 m_idx 找到与父进程通信的管道 */
    int pipefd = m_sub_process[m_idx].m_pipefd[ 1 ];
    /* 子进程需要监听管道文件描述符 pipefd，因为父进程将通过它来通知子进程 accept 新连接 */
    addfd( m_epollfd, pipefd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    T* users = new T[ USER_PER_PROCESS ];
    assert( users );
    int number = 0;
    int ret = -1;

    while ( !m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( (number < 0) && ( errno != EINTR ) ) {
            printf( "epoll faillure\n" );
            break;
        }

        for ( int i = 0; i < number ; ++i ) {
            int sockfd = events[i].data.fd;
            if ( (sockfd == pipefd ) && ( events[i].events && EPOLLIN ) ) {
                int client = 0;
                /* 从父、子进程之间的管道读取数据，并将结果保存在变量 client 中，如果读取成功，
                则表示有新客户连接到来 */
                ret = recv( sockfd, (char*)&client, sizeof( client ), 0 );
                if ( ((ret < 0 ) && ( errno != EAGAIN)) || ret == 0 ) {
                    continue;
                }
                else {
                    struct sockaddr_in client_address;
                    socklen_t client_addresslength = sizeof( client_address);
                    int connfd = accept( m_listenfd, (struct sockaddr*)&client_address, &client_addresslength );
                    if ( connfd < 0 ) {
                        printf( "accpect error, errno is %d", errno );
                        continue;
                    }
                    addfd( m_epollfd, connfd );
                    /* 模板类 T 必须实现 init 方法，以初始化一个客户连接，我们直接用 connfd 来索引
                    逻辑处理对象（T 类型的对象），以提高程序效率 */
                    users[connfd].init( m_epollfd, connfd, client_address );
                }
            }
            else if ( ( sockfd == sig_pipefd[0] ) &&  ( events[i].events & EPOLLIN ) ) {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof(signals), 0 );
                if ( ret <= 0 ) {
                    continue;
                }
                else {
                    for ( int i = 0; i < ret ; ++i ) {
                        switch ( signals[i] )
                        {
                        case SIGCHLD:
                            pid_t pid;
                            int stat;
                            while ( (pid = waitpid(-1,&stat,WNOHANG ) ) > 0 ) {
                                continue;
                            }
                            break;

                        case SIGTERM:
                        case SIGINT:
                            m_stop = true;
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            /* 如果是其他可读数据，那么必然是客户请求到来，调用逻辑处理对象的 process 方法处理之 */
            else if ( events[i].events & EPOLLIN ) {
                users[ sockfd ].process();
            }
            else {
                continue;
            }
        }
    }
    
    delete []users;
    users = NULL;
    close( pipefd );
    // close( m_listenfd); /* 应该由 m_listenfd 的创建者来关闭这个文件描述符（在后面），即所谓的对象由哪个函数创建，就应该由哪个函数销毁 */
    close( m_epollfd );
}



template<typename T>
void processpoll<T>::run_parent () {
    setup_sig_pipe( );

    /* 父进程监听 m_listenfd */
    addfd( m_epollfd, m_listenfd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;  // 用于 RR 方式分配任务
    int new_conn  =  1;
    int number = 0;
    int ret = -1;

    while ( !m_stop ) {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0 ; i < number; ++i ) {
            int sockfd = events[i].data.fd;
            if ( sockfd == m_listenfd ) {
                /* 如果有新连接到来，就采用 RR 方式将其分配给一个子进程处理 */
                int index = sub_process_counter;
                do
                {
                    if( m_sub_process[index].m_pid != -1 ) {
                        break;
                    }
                    index = ( index + 1) % m_process_number;
                }
                while ( index != sub_process_counter );

                if ( m_sub_process[index].m_pid == -1 ){
                    m_stop = true;
                    break;
                }
                sub_process_counter = (index+1) % m_process_number;
                send ( m_sub_process[index].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                printf( "send request to child %d\n", index );
            }
            else if  ( ( sockfd == sig_pipefd[0]) && ( events[i].events & EPOLLIN ) ) {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if ( ret <= 0 ) {
                    continue;
                }
                else {
                    for ( int j = 0; j < ret; ++j ) {
                        switch ( signals[j] ) 
                        {
                        case SIGCLD:
                        {
                            pid _t pid;
                            int stat;
                            while ( ( pid = waitpid(-1,&stat, WNOHANG ) ) > 0 ) {
                                for ( int k = 0; k < m_process_number; ++k ) {
                                    if ( m_sub_process[k].m_pid == pid ) {
                                        printf( "child %d join\n", k );
                                        m_sub_process[i].m_pid = -1;
                                    }
                                }
                            }
                            /* 如果所有子进程都已经退出了，则父进程也退出 */
                            m_stop = true;
                            for ( int k = 0; k < m_process_number; ++k ){
                                if ( m_sub_process[k].m_pid != -1 ) {
                                    m_stop =false;
                                    break;
                                }
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT:
                        {
                            /* 如果父进程接收到终止信号，那么就杀死所有子进程，并等待它们全部结束。当然，通知子进程结束更好的方法是向父、
                                子进程之间的通信管道发送特殊数据，后面可以自己实现一下 */
                            printf( "kill all the child now\n" );
                            for ( int j = 0; j < m_process_number; ++i ) {
                                int pid = m_sub_process[i].m_pid;
                                if ( pid != -1 ) {
                                    kill( pid, SIGTERM );
                                }
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
            else {
                continue;
            }
        }
    }
    // close( m_listenfd );  /* 由创建者关闭这个文件描述符 （见后文）*/
    close( m_epollfd );
}


#endif