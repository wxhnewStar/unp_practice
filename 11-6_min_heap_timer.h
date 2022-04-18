#ifndef MIN_HEAP_TIMER
#define MIN_HEAP_TIMER

#include <iostream>
#include <arpa/inet.h>
#include <time.h>
using std::exception;

#define BUFFER_SIZE 64

class heap_timer; /* 前向声明 */

/* 绑定 socket 和定时器 */
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[ BUFFER_SIZE ];
    heap_timer* timer;
};

class heap_timer
{
public:
    time_t expire;
    void (*cb_func) ( client_data* ); /* 定时器的回调函数 */
    client_data* user_data; /* 用户数据 */
public:
    heap_timer( int delay ) {
        expire = time( NULL ) + delay;
    }
    ~heap_timer();
};


/* 时间堆类 */
class time_heap {
private:
    heap_timer** array; /* 堆数组 */
    int capacity;  /* 堆数组的容量 */
    int cur_size;  /* 堆数组当前包含元素的个数 */

public:
    time_heap( int cap ) throw( std::exception ): capacity( cap ), cur_size( 0 ) {
        array = new heap_timer* [capacity];  /* 创建堆数组 */
        if ( !array ) {
            throw std::exception();
        }
        for( int i = 0; i < capacity; ++i ) {
            array[i] = NULL;
        }
    }

    time_heap( heap_timer** init_array, int size, int cap ) throw( std::exception):
        cur_size( size ), capacity( cap ) {
        if ( capacity < size ) {
            throw std::exception();
        }
        array = new heap_timer* [capacity]; /* 创建堆数组 */
        if ( !array ) {
            throw exception();
        }
        for( int i = 0; i < capacity; ++i ) {
            array[i] = NULL;
        }
        if ( size != 0 ) {
            /* 初始化堆数组 */
            for ( int i = 0 ; i < size; ++i ) {
                array[i] = init_array[i];
            }
            for ( int i = (cur_size - 1) / 2; i >=0; --i ) {
                /* 对数组中的第 [(cur_size -1 )/2] ~ 0个元素执行 下沉 操作 */
                percolate_down( i );
            }
        }
    }

    /* 销毁时间堆 */
    ~time_heap() {
        for( int i = 0; i < cur_size; ++i ) {
            delete array[i];
        }
        delete []array;
    }

public:
    /* 添加目标定时器 timer */
    void add_timer( heap_timer* timer ) throw( std::exception ) {
        if ( !timer ) {
            return;
        }
        if ( cur_size >= capacity ) {
            resize();  /* 如果当前堆数组容量不够，则将其扩大 1 倍 */
        }
        /* 新插入了一个元素，当前堆大小加1，hole 是新建空穴的位置 */
        int hole = cur_size++;
        int parent = 0;
        /* 对从空穴到根节点的路径上的所有节点执行 上浮操作 */
        for ( ; hole > 0; hole = parent ) {
            parent = ( hole - 1 ) / 2;
            if ( array[parent]->expire <= timer->expire )
                break;
            array[hole] = array[parent];
        }
        array[hole] = timer;
    }

    /* 删除目标定时器 timer */
    void del_timer( heap_timer* timer ) {
        if ( !timer ) {
            return;
        }
        /* 仅仅将目标定时器的回调函数设置为空，即所有的延迟销毁。这将节省真正删除该定时器造成的开销，
            不过容易使数组膨胀 */
        timer->cb_func = NULL;
    }

    /* 获得堆顶部的定时器 */
    heap_timer* top() const {
        if ( empty() ) {
            return NULL;
        }
        return array[0];
    }

    /* 删除堆顶部的定时器 */
    void pop_timer( ) {
        if ( empty() ) {
            return ;
        }
        if ( array[0] ) {
            delete array[0];
            /* 将原来的堆顶元素替换为堆数组中最后一个元素 */
            array[0] = array[--cur_size];
            procolate_down( 0 ); /* 对新的堆顶元素执行 下沉 操作 */
        }
    }

    /* 心搏函数 */
    void tick( ) {
        heap_timer* tmp = array[0];
        time_t cur = time( NULL ); /* 循环处理堆中到期的定时器 */
        while ( !empty() ) {
            if ( !tmp ) {
                break;
            }
            /* 如果堆顶定时器没有到期，则退出循环 */
            if ( tmp->expire > cur ) {
                break;
            }
            /* 否则就执行堆顶计时器中的任务 */
            if ( array[0]->cb_func ) {
                array[0]->cb_func( array[0]->user_data );
            }
            /* 将堆顶元素删除，同时生成新的堆顶定时器 (array[0]) */
            pop_timer();
            tmp = array[0];
        }
    }

    bool empty() const {
        return cur_size == 0;
    }

private:
    /* 最小堆的 下沉 操作，它确保数组中以第 hole 个节点作为根的子树拥有最小堆性质*/
    void percolate_down( int hole ) {
        heap_timer* temp = array[hole];
        int child = 0;
        for ( ; ( (hole*2 + 1) <= (cur_size-1) ); hole = child ) {
            child = hole * 2 + 1;
            if ( (child < (cur_size-1) ) && (array[child+1]->expire < array[child]->expire ) ) {
                ++child;
            }
            if ( array[child]->expire < temp->expire ) {
                array[hole] = array[child];
            }
            else {
                break;
            }
        }
        array[hole] = temp;
    }

    /* 将堆数组容量扩大 1 倍 */
    void resize() throw( std::exception ) {
        heap_timer** temp = new heap_timer* [2*capacity];
        if ( !temp ) {
            throw std::exception();
        }
        for( int i = 0; i < 2*capacity; ++i ) {
            temp[i] = NULL;
        }
        
        capacity = 2 * capacity;
        for ( int i = 0; i < cur_size; ++i ){
            temp[i] = array[i];
        }
        delete []array;
        array = temp;
    }
};

#endif