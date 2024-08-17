#include "threadpool.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#define UPDATE_NUM 2
//任务队列结构体
typedef struct Task{
    void (*function)(void * arg);
    void* arg;
}Task;

//线程池结构体
struct ThreadPool{
    //任务队列相关
    Task* task;
    int queueSize;      //任务数量
    int queueCapacity;  //队列容量
    int queueFront;     //队头
    int queueRear;      //队尾

    pthread_t managerID;    //管理者进程
    pthread_t *threadIDs;   //消费者进程（工作进程）
    int min;                //最小进程数
    int max;                //最大进程数
    int busyNum;            //繁忙的进程数
    int liveNum;            //存活的进程数
    int exitNum;            //要销毁的进程数
    //锁
    pthread_mutex_t mutexPool;       //锁整个线程池
    pthread_mutex_t mutexBusyNum;    //锁繁忙进程数
    //阻塞信号
    pthread_cond_t notEmpty;         //判断队列为空，阻塞
    pthread_cond_t notFull;          //判断队列已满，阻塞

    int shutdown;            //关闭线程池变量
};

ThreadPool* threadPoolCreate(int min, int max, int queueSize){
    ThreadPool* pool = (ThreadPool *)malloc(sizeof(ThreadPool));

    do{
        if(pool == NULL){
            printf("malloc threadpool fail...\n");
                break;
        }

        pool-> threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max);
        if (pool->threadIDs == NULL)
        {
            printf("malloc threadIDs fail...\n");
            break;
        }
        memset(pool-> threadIDs, 0, sizeof(pthread_t) * max);
        pool->min = min;
        pool->max = max;
        pool->busyNum = 0;
        pool->exitNum = 0;
        pool->liveNum = min;

        if(pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
        pthread_mutex_init(&pool->mutexBusyNum, NULL) != 0 ||
        pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
        pthread_cond_init(&pool->notFull, NULL) != 0){
            printf("mutex or condition init fail...\n");
            break;
        }

        pool->task = (Task *)malloc(sizeof(Task)* queueSize);
        if (pool->task == NULL)
        {
            printf("malloc task fail...\n");
            break;
        }

        pool-> queueSize = 0;
        pool-> queueCapacity = queueSize;
        pool-> queueFront = 0;
        pool-> queueRear = 0;

        pool->shutdown = 0;
        
        //创建线程
        pthread_create(&pool->managerID, NULL, manager, pool);
        for(int i = 0; i < min; i++){
            pthread_create(&pool->threadIDs[i], NULL, worker, pool);
        }
        return pool;
    }while(0);

    if(pool && pool->threadIDs) free(pool->threadIDs);
    if(pool && pool->task) free(pool->task);
    if(pool) free(pool);

    return NULL;
}

int threadPoolDestroy(ThreadPool *pool){
    if(pool == NULL){
        return -1;
    }
    pool->shutdown = 1;

    pthread_join(pool->managerID, NULL);

    for(int i = 0; i < pool->liveNum; i++){
        pthread_cond_signal(&pool->notEmpty);
    }
    //唤醒线程后，等待线程销毁再释放资源
    // sleep(1);

    // 释放堆内存
    if (pool->task)
    {
        free(pool->task);
    }
    if (pool->threadIDs)
    {
        free(pool->threadIDs);
    }

    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusyNum);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);

    free(pool);
    pool = NULL;

    return 0;
}

void threadPoolAdd(ThreadPool *pool, void(*func)(void *arg), void *arg){
    pthread_mutex_lock(&pool->mutexPool);
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        // 阻塞生产者线程
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }
    pool->task[pool->queueRear].arg = arg;
    pool->task[pool->queueRear].function = func;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexBusyNum);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusyNum);
    return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return aliveNum;
}

//从工作队列中选择 工作分配给工作线程
void* worker(void *arg){
    ThreadPool* pool = (ThreadPool *)arg;
    while(1){
        pthread_mutex_lock(&pool->mutexPool);
        while(pool->queueSize == 0 && !pool->shutdown){
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

            if(pool->exitNum > 0){
                pool->exitNum--;
                if(pool->liveNum > pool->min){
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }
        }
        if(pool->shutdown){
            pthread_mutex_unlock(&pool->mutexPool);
            //pthread_exit(NULL);
            //不仅要退出线程，还要把线程赋值为0，这样方便我们后面的判断if(pool->threadIDs[i] == 0)
            threadExit(pool);
        }
        Task task;
        task.function = pool->task[pool->queueFront].function;
        task.arg = pool->task[pool->queueFront].arg;
        pool->queueFront = (pool->queueFront + 1) % (pool->queueCapacity);
        pool->queueSize--;

        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);

        pthread_mutex_lock(&pool->mutexBusyNum);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusyNum);
        //线程开始工作
        task.function(task.arg);
        free(task.arg);
        task.arg = NULL;

        printf("thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusyNum);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusyNum);
    }
    return NULL;
}

//根据 线程数量和任务数量，调整线程数量。
void* manager(void *arg){
    ThreadPool* pool = (ThreadPool *)arg;
    while(!pool->shutdown){
        sleep(3);
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);

        pthread_mutex_lock(&pool->mutexBusyNum);
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusyNum);

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

void threadExit(ThreadPool *pool){
    pthread_t tid = pthread_self();
    for(int i = 0; i < pool->max; ++i){
        // printf("pool->threadIDs【i】=%ld tid=%ld\n", pool->threadIDs[i], tid);
        if(pool->threadIDs[i] == tid){
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting...\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}