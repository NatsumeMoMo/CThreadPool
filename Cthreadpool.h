#include<windows.h>
//任务结构体
typedef struct Task
{
    /* data */
    void (*functionptr)(void* funcargs);
    void* funcargs;
}Task;

//线程池结构体
struct Cthreadpool
{
    /* Task Queue */
    //操作任务队列及相关数据时，使用mutex锁来保证互斥
    Task* TaskQueue;
    int Taskmax;//最大容量
    int CurrnetTasknum;//当前任务容量
    int front;//对头
    int rear;//d队尾


    HANDLE ThreadManger;//管理者线程句柄
    HANDLE* ThreadWorkers;//工作线程句柄
    DWORD ThreadMangerID;//管理者线程标识符
    DWORD* ThreadWorkersID;//工作线程标识符
    int MaxThreadnum;//最大线程数
    int MinThreadnum;//最小线程数
    int WorkingThreadnum;//当前工作线程数----------->灵活变量，需要考虑单独上锁
    int liveThreadnum;//存活线程数
    int NeedtoKillnum;//需要杀死的线程个数（当存活个数大于工作个数时）
    CRITICAL_SECTION mutexPool;//整个线程池的锁
    CRITICAL_SECTION mutexWorkingnum;//单独对 WorkingThreadnum 上锁

    int IsDestoryPool; //是否要销毁线程池,销毁为1，否则为0
    CONDITION_VARIABLE isTaskFull; //任务队列是否已满--->生产者条件变量
    CONDITION_VARIABLE isTaskEmpty; //任务队列是否为空--->消费者条件变量
};

const int PerThreadnum=2;//每次 添加\销毁 的线程数

/*
在Windows的多线程编程中，使用 CreateThread() 函数创建线程时，传入
的线程函数必须满足特定的函数签名。这个函数必须返回 DWORD 类型，并且
接受一个 LPVOID 类型的参数。这是Windows API的要求
DWORD WINAPI ThreadFunction(LPVOID lpParam);

在POSIX线程（pthread）中，线程函数的要求稍有不同。在 pthread_create() 
函数中，线程函数必须返回 void* 类型，并且接受一个 void* 类型的参数
void* ThreadFunction(void* arg);

这两种线程函数的设计都是为了提供足够的灵活性，让你可以传递任意类型的
参数给线程函数
*/
DWORD ThreadMangerFunction(LPVOID laParam);
DWORD ThreadWokerFunction(LPVOID lpParam);


Cthreadpool* createThreadPool(int minThreadnum, int maxThreadnum, int taskmax);
DWORD ThreadWokerFunction(LPVOID laParam);
DWORD ThreadMangerFunction(LPVOID laParam);
void threadExit(Cthreadpool* pool);
int destroyThreadPool(Cthreadpool* pool);
void addTask(Cthreadpool* pool,void(*funcptr)(void*),void* funcarg);
int getWorkingThreadnum(Cthreadpool* pool);
int getLiveThreadnum(Cthreadpool* pool);