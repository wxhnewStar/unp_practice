#ifndef THREADPOLL_H
#define THREADPOLL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "14-2_locker.h"


/* 线程池类，将它定义为模板类是为了代码复用。模板参数 T 是任务类 */
template< typename T>
class threadpoll
{
public:
    threadpoll( int thread_number = 8, int max_requests = 10000 );
    ~threadpoll();
    /* 往请求队列中添加任务 */
    bool append( T* request );

private:
    /* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
    static void* worker( void* arg );
    void run();

private:
    int m_thread_number; // 线程池中的线程数
    int m_max_requests;  // 请求队列中允许的最大请求数
    pthread_t* m_threads; // 描述线程池的数组，其大小为 m_thread_number
    std::list<T*> m_workqueue; // 是否有任务需要处理
    locker m_queuelocker;  // 是否有任务需要处理
    sem m_queuestat;  // 是否有任务需要处理 
    bool m_stop; // 是否结束线程
};

template< typename T>
threadpoll< T >::threadpoll( int thread_number = 8, int max_requests = 1000 ):
    m_thread_number( thread_number), m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if ( (thread_number <= 0 ) || ( max_requests <= 0 ) )
    {
        throw std::exception();
    }

    m_threads = new pthread_t[ m_thread_number ];
    if ( !m_threads )
    {
        throw std::exception();
    }
    
    /* 创建 thread_number 个线程，并将它们都设置为脱离线程 */
    for ( int i = 0; i < thread_number; ++i )
    {
        printf( "create the %dth thread\n", i );
        if ( pthread_create( m_threads + i, NULL, worker, this ) != 0 )  // this 指针啥意思？传入的是这个对象？
        {
            delete [] m_threads;
            m_threads = nullptr;
            throw std::exception();
        }
        if ( pthread_detach( m_threads[i] ) ) {
            delete []m_threads;
            m_threads = nullptr;
            throw std::exception();
        }
    }
}

template< typename T>
threadpoll<T>::~threadpoll()
{
    if ( !m_threads )  // 我自己加的，以防前面初始化过程中的 delete完了， 重复 delete，but 貌似构造函数里抛出异常，系统会自动回收
        delete []m_threads;
    m_stop = true;
}


template <typename T>
bool threadpoll< T >::append( T* request ) {
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if ( m_workqueue.size() > m_max_requests )
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/* 每个线程的工作内容实际上是调用 threadpoll 上的 run() 函数，因此传入参数是 this 指针 */
template< typename T>
void* threadpoll<T>::worker( void* arg ) {
    threadpoll* poll = ( threadpoll* ) arg;
    pool->run();
    return poll;
}

template<typename T>
void threadpoll<T>::run() {
    while ( !m_stop )
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if( m_workqueue.empty() ) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if( !request ) {
            continue;
        
        }
        request->process();
    }
}

#endif