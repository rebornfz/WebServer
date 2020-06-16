#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 2, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    int m_thread_number;
    int m_max_requests;
    pthread_t *m_threads;//定义线程
    std::list<T *> m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;
    connection_pool *m_connPool;//数据库
    int m_actor_model;

    static void *worker(void *arg);
    void run;
};

template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool){
    if(thread_number <= 0 || max_requests <= 0){
        throw std::exception();
    }
    m_thread = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }
    for(int i = 0; i < thread_number; i++){
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
bool threadpool<T>::append(T *request, int state){
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;//m_state是啥？
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker(void *arg){
    threadpool *pool = (threadpool *) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(true){
        m_queuestate.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        if(m_actor_model == 1){
            if(request->m_state == 0){
                if(request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else{
                if(request->write()){
                    request->improv = 1;
                }
                else{
                    request->improv = 1;
                    requset->timer_flag = 1;
                }
            }
            else{
                connectionRAII mysqlcon(&request->mysql, m_connPool);
                request->process();
            }
        }
    }
}

#endif