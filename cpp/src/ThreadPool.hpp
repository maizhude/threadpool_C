#include"TaskQueue.hpp"
#include<iostream>
#include<pthread.h>
#include<string.h>
#include<string>
#include<unistd.h>
using namespace std;

template <typename T>
class ThreadPool{
public:
    ThreadPool(int min, int max);
    ~ThreadPool();

    void addTask(Task<T> task);

    int getBusyNum();

    int getAliveNum();

private:

    static void* manager(void *arg);
    static void* worker(void *arg);

    void threadExit();
private:
    TaskQueue<T> *taskQ;
    pthread_t managerID;    //管理者进程
    pthread_t *threadIDs;   //消费者进程（工作进程）
    int min;                //最小进程数
    int max;                //最大进程数
    int busyNum;            //繁忙的进程数
    int liveNum;            //存活的进程数
    int exitNum;            //要销毁的进程数
    //锁
    pthread_mutex_t mutexPool;       //锁整个线程池
    //阻塞信号
    pthread_cond_t notEmpty;         //判断队列为空，阻塞
    static const int UPDATE_NUM = 2;
    bool shutdown;            //关闭线程池变量
};

template <typename T>
ThreadPool<T>::ThreadPool(int min, int max){
    do{

        threadIDs = new pthread_t[max];
        if (threadIDs == nullptr)
        {
            cout << "malloc threadIDs fail..." << endl;
            break;
        }
        memset(threadIDs, 0, sizeof(pthread_t) * max);
        min = min;
        max = max;
        busyNum = 0;
        exitNum = 0;
        liveNum = min;

        if(pthread_mutex_init(&mutexPool, NULL) != 0 ||
            pthread_cond_init(&notEmpty, NULL) != 0){
            cout <<"mutex or condition init fail..." << endl;
            break;
        }

        taskQ = new TaskQueue<T>();
        if (taskQ == NULL)
        {
            cout <<"malloc task fail..." << endl;
            break;
        }


        shutdown = false;
        
        //创建线程
        pthread_create(&managerID, NULL, manager, this);
        for(int i = 0; i < min; i++){
            pthread_create(&threadIDs[i], NULL, worker, this);
        }
        return;
    }while(0);

    if(threadIDs) delete[] threadIDs;
    if(taskQ) delete taskQ;
}
template <typename T>
ThreadPool<T>::~ThreadPool(){
    
    shutdown = true;

    pthread_join(managerID, NULL);

    for(int i = 0; i < liveNum; i++){
        pthread_cond_signal(&notEmpty);
    }
    //唤醒线程后，等待线程销毁再释放资源
    // sleep(1);

    // 释放堆内存
    if (taskQ)
    {
        delete taskQ;
    }
    if (threadIDs)
    {
        delete[] threadIDs;
    }

    pthread_mutex_destroy(&mutexPool);
    pthread_cond_destroy(&notEmpty);
}
template <typename T>
void ThreadPool<T>::addTask(Task<T> task){
    if (shutdown)
    {
        return;
    }
    taskQ->addTask(task);

    pthread_cond_signal(&notEmpty);
}
template <typename T>
int ThreadPool<T>::getBusyNum(){
    pthread_mutex_lock(&mutexPool);
    int busyNum = this->busyNum;
    pthread_mutex_unlock(&mutexPool);
    return busyNum;
}
template <typename T>
int ThreadPool<T>::getAliveNum(){
    pthread_mutex_lock(&mutexPool);
    int liveNum = this->liveNum;
    pthread_mutex_unlock(&mutexPool);
    return liveNum;
}

//从工作队列中选择 工作分配给工作线程
template <typename T>
void* ThreadPool<T>::worker(void *arg){
    ThreadPool* pool = static_cast<ThreadPool *> (arg);
    while(1){
        pthread_mutex_lock(&pool->mutexPool);
        while(pool->taskQ->getTaskSize() == 0 && !pool->shutdown){
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

            if(pool->exitNum > 0){
                pool->exitNum--;
                if(pool->liveNum > pool->min){
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    pool->threadExit();
                }
            }
        }
        if(pool->shutdown){
            pthread_mutex_unlock(&pool->mutexPool);
            //pthread_exit(NULL);
            //不仅要退出线程，还要把线程赋值为0，这样方便我们后面的判断if(pool->threadIDs[i] == 0)
            pool->threadExit();
        }
        Task<T> task;
        task = pool->taskQ->getTask();
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexPool);

        //线程开始工作
        task.function(task.arg);
        delete task.arg;
        // task.arg = nullptr;

        cout << "thread "  << to_string(pthread_self()) <<  " end working..." << endl;
        pthread_mutex_lock(&pool->mutexPool);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexPool);
    }
    return NULL;
}

//根据 线程数量和任务数量，调整线程数量。
template <typename T>
void* ThreadPool<T>::manager(void *arg){
    ThreadPool* pool = static_cast<ThreadPool *> (arg);
    while(!pool->shutdown){
        sleep(3);
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->taskQ->getTaskSize();
        int liveNum = pool->liveNum;
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexPool);


        //线程少，任务多，新增线程数量，每次+2
        if(liveNum < queueSize && liveNum < pool->max){
            pthread_mutex_lock(&pool->mutexPool);
            int count = 0;
            for(int i = 0; i < pool->max && count < UPDATE_NUM
                && pool->liveNum < pool->max; i++){
                    if(pool->threadIDs[i] == 0){
                        pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                        count++;
                        pool->liveNum++;
                    }
                }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        //线程多，任务少，销毁线程数量，每次-2
        if(busyNum * 2 < liveNum && liveNum > pool->min){
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = UPDATE_NUM;
            pthread_mutex_unlock(&pool->mutexPool);
            //让工作的线程自杀
            for(int i = 0; i < UPDATE_NUM; ++i){
                pthread_cond_signal(&pool->notEmpty);
            }
        }
    }
    return NULL;
}
template <typename T>
void ThreadPool<T>::threadExit(){
    pthread_t tid = pthread_self();
    for(int i = 0; i < max; ++i){
        // printf("pool->threadIDs【i】=%ld tid=%ld\n", pool->threadIDs[i], tid);
        if(threadIDs[i] == tid){
            threadIDs[i] = 0;
            cout << "threadExit() called, "<< to_string(tid) << " exiting..." << endl;
            break;
        }
    }
    pthread_exit(NULL);
}