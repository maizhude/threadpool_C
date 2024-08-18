// #ifndef _THREADPOOL_H
// #define _THREADPOOL_H

typedef struct ThreadPool ThreadPool;

//创建线程池并初始化
ThreadPool * threadPoolCreate(int min, int max, int queueSize);

//销毁线程池
int threadPoolDestroy(ThreadPool *pool);

//向线程池中添加任务
void threadPoolAdd(ThreadPool *pool, void(*func)(void *arg), void *arg);

int threadPoolBusyNum(ThreadPool* pool);

int threadPoolAliveNum(ThreadPool* pool);

void* manager(void *arg);
void* worker(void *arg);

void threadExit(ThreadPool *pool);