#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>

#define BUFFER_SIZE 64
class util_timer;  // 前向声明

/* 用户数据结构： 客户端 socket 地址、socket 文件描述符、读缓存和定时器 */
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[ BUFFER_SIZE ];
    util_timer* timer;
};

/* 定时器类 */
class util_timer
{
public:
    util_timer* next;   /* 指向前一个定时器 */
    util_timer* prev;   /* 指向后一个定时器 */
    client_data* user_data; 
    time_t expire; /* 任务的超时时间，这里使用绝对时间 */
public:
    util_timer() : prev( NULL ), next( NULL) { }
    ~util_timer() = default;

    void (*cb_func) ( client_data* ); /* 任务回调函数 */
};

/* 定时器链表。 它是一个升序、双向链表，且带有头结点和尾节点 */
class sort_timer_lst {
private:
    util_timer* head;
    util_timer* tail;
public:
    sort_timer_lst() : head( NULL ), tail( NULL ) { }
    
    /* 链表被销毁时，删除其中所有的定时器 */
    ~sort_timer_lst( ) {
        util_timer* tmp = head;
        while ( tmp ) {
            head = tmp -> next;
            delete tmp;
            tmp = head;
        }
    }

    /* 将目标定时器 timer 添加到链表中 */
    void add_timer( util_timer* timer ) {
        if ( !timer ) { // 非法输入
            return ; 
        }
        if( !head ) { // 链表为空
            head = tail = timer;
            return;
        }
        /* 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，则把该定时器插入链表头部，作为链表新的头结点。
            否则就需要调用重载函数 add_timer( util_timer* timer, util_timer* lst_head )，把它插入链表中合适的位置，
            以保证链表的升序特性 */
        if ( timer->expire < head->expire ) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer( timer, head );
    }

    /* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。这个函数只考虑被调整的定时器的超时时间延长的情况，即
        该定时器需要往链表的尾部移动 */
    void adjust_timer( util_timer* timer ) {
        if ( !timer )
            return ;
        util_timer* tmp = timer->next;
        /* 如果被调整到额目标定时器处在链表尾部，或者该定时器新的超时值仍然小于其下一个定时器的超时值，则无需调整 */
        if ( !tmp || tmp->expire >= timer->expire ) {
            return ;
        }
        /* 如果目标定时器是链表的头结点，则将该定时器从链表中取出并重新插入链表 */
        if ( timer == head ) {
            head = head -> next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer( timer, head );
        }
        /* 如果目标定时器不是链表的头结点，则将该定时器从链表中取出来，然后插入其原来所在位置之后的部分链表中 */
        else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer( timer, timer->next );
        }
    }

    /* 将目标定时器 timer 从链表中删除 */
    void del_timer( util_timer* timer ) {
        if ( !timer ) {
            return;
        }
        /* 下面这个条件成立表示链表中只有一个定时器，即目标定时器 */
        if ( ( timer == head ) && ( timer == tail ) ) {
            delete timer;
            head = NULL;
            tail = NULL;
            return ;
        }
        /* 如果链表中至少有两个定时器，且目标定时器是链表的头结点，则将链表的头结点重置为原头结点的下一个节点，然后删除目标定时器 */
        if ( timer ==  head ) {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        /* 同上，但是是尾节点 */
        if ( timer == tail ) {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        /* 如果目标定时器位于链表的中间，则把它前后的定时器串联起来，然后删除目标定时器 */
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    /* SIGALRM 信号每次被触发就在其信号处理函数（如果使用统一事件源，则是主函数）中执行一次 tick 函数，以处理链表上到期的任务 */
    void tick() {
        if ( !head ) {
            return;
        }
        printf( "timer tick\n" );
        time_t cur = time( NULL );  /* 获取当前的系统时间 */
        util_timer* tmp = head;
        /* 从头结点开始依次处理每个定时器，直到遇到一个尚未到期的定时器，这就是定时器的核心逻辑 */
        while( tmp ) {
            /* 因为每个定时器都使用绝对时间作为超时值，所以我们可以把定时器的超时值和系统当前时间作比较，以判断定时器是否到期 */
            if ( cur < tmp->expire ) {
                break;
            }
            /* 调用定时器的回调函数，以执行定时任务 */
            tmp->cb_func( tmp->user_data );
            /* 执行完定时器的任务之后，就将它从链表中删除，并重置链表头结点 */
            head = tmp->next;
            if ( head ) {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    /* 一个重载的辅助函数，它被共有的 add_timer 函数和 adjust_timer 函数调用。该函数表示将目标定时器 timer 添加到节点 lst_head 之后的部分链表中 */
    void add_timer( util_timer* timer, util_timer* lst_head ) {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        /* 遍历 lst_head 节点之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间的节点，并
            将目标定时器插入该节点之前 */
        while ( tmp ) {
            if ( timer->expire < tmp->expire ) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        
        /* 如果遍历完 lst_head 节点之后的部分链表，仍为找到超时时间大于目标定时器的超时时间的节点，则将目标定时器插入链表尾部，并把它设置为
            链表新的尾节点 */
        if ( !tmp ) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }
};

#endif