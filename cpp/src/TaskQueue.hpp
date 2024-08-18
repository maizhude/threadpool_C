#include<queue>
#include<pthread.h>

using callback = void (*)(void * arg);

template <typename T>
struct Task{
    Task<T>(){
        function = nullptr;
        arg = nullptr;
    }
    Task<T>(callback f ,void* arg){
        function = f;
        this->arg = (T*)arg;
    }
    callback function;
    T* arg;
};

template <typename T>
class TaskQueue{

public:
    //构造与析构函数
    TaskQueue();
    ~TaskQueue();

    //添加任务
    void addTask(Task<T> task);
    void addTask(callback f, void* arg);
    //获取任务
    Task<T> getTask();
    //获取当前任务个数
    inline size_t getTaskSize(){
        return m_taskQ.size();
    }

private:
    std::queue<Task<T>> m_taskQ;
    pthread_mutex_t m_taskLock;
};
//构造与析构函数
template <typename T>
TaskQueue<T>::TaskQueue(){
    pthread_mutex_init(&m_taskLock, NULL);
}

template <typename T>
TaskQueue<T>::~TaskQueue(){
    pthread_mutex_destroy(&m_taskLock);
}

//添加任务
template <typename T>
void TaskQueue<T>::addTask(Task<T> task){
    pthread_mutex_lock(&m_taskLock);
    m_taskQ.push(task);
    pthread_mutex_unlock(&m_taskLock);
}

template <typename T>
void TaskQueue<T>::addTask(callback f, void* arg){
    pthread_mutex_lock(&m_taskLock);
    m_taskQ.push(Task<T>(f, arg));
    pthread_mutex_unlock(&m_taskLock);
}
//获取任务
template <typename T>
Task<T> TaskQueue<T>::getTask(){
    Task<T> t;
    pthread_mutex_lock(&m_taskLock);
    if(!m_taskQ.empty()){
        t = m_taskQ.front();
        m_taskQ.pop(); 
    }
    
    pthread_mutex_unlock(&m_taskLock);
    return t;
}