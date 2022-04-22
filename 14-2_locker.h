#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem {
public:
    /* 创建并初始化信号量 */
    sem() {
        if ( sem_init( &m_sem, 0, 0 ) != 0 ) {
            /* 构造函数没有返回值，可以通过抛出异常来报告错误 */
            throw std::exception(" sem_init " );
        }
    }
    /* 销毁信号量 */
    ~sem() {
        sem_destroy( &m_sem );
    }
    /* 等待信号量 */
    bool wait() {
        return sem_wait( &m_sem ) == 0 ;
    }
    /* 释放信号量 */
    bool post() {
        return sem_post( &m_sem ) == 0;
    }


private:
    sem_t m_sem;
};


/* 封装互斥锁的类 */
class locker {
public:
    locker() {
        if ( pthread_mutex_init( &m_mutex, NULL ) != 0 ) {
            /* 构造函数没有返回值，可以通过抛出异常来报告错误 */
            throw std::exception(" locker_init " );
        }
    }
    /* 销毁信号量 */
    ~locker() {
        pthread_mutex_destroy( &m_mutex );  // 不是成功返回 0，失败返回 -1 吗？
    }
    /* 等待信号量 */
    bool lock() {
        return pthread_mutex_lock( &m_mutex ) == 0;
    }
    /* 释放信号量 */
    bool unlock() {
        return pthread_mutex_unlock( &m_mutex ) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};


/* 封装条件变量的类 */
class cond {
public:
    cond() {
        if ( pthread_cond_init( &m_cond, NULL ) != 0 ) {
            throw std::exception( "cond_init" );
        }
        if ( pthread_mutex_init( &m_mutex, NULL ) != 0 ) {
            throw std::exception( "mutex_init" );
        }
    }
    ~cond() {
        pthread_mutex_destroy( &m_mutex );
        pthread_cond_destroy( &m_cond );
    }

    bool wait() {
        int ret = 0;
        pthread_mutex_lock( &m_mutex );
        ret = pthread_cond_wait( &m_cond, &m_mutex );
        pthread_mutex_unlock( &m_mutex );
        return ret == 0; 
    }

    bool signal() {
        return pthread_cond_signal( &m_cond ) == 0;
    }

private:
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;
};

#endif