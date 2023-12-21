#include "Cthreadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <unistd.h>
// 创建线程池
Cthreadpool *createThreadPool(int minThreadnum, int maxThreadnum, int taskmax)
{
    Cthreadpool *pool = (Cthreadpool *)malloc(sizeof(Cthreadpool));

    /*
    将以下逻辑代码放入到一个 do-while 循环中是因为一开始给 pool 分配了堆内存
    ，而其中有三个错误的 if 判断，每次判断失误在返回NULL之前都要释放之前的申请
    的堆内存，就显得很麻烦。放入 do-while(0) 循环中后，这个循环只会执行一次但
    其中的 if 判断却可以使用 break 跳出循环。这样只需在最后统一释放内存，再返
    回NULL即可
    */
    do
    {
        if (pool == nullptr)
        {
            printf("Create ThreadPool failed !\n");
            break; // return NULL;
        }

        pool->ThreadWorkers = (HANDLE *)malloc(maxThreadnum * sizeof(HANDLE));
        if (pool->ThreadWorkers == nullptr)
        {
            printf("Failed to allocate memory to ThreadWorker !\n");
            break; // return NULL;
        }
        memset(pool->ThreadWorkers, NULL, maxThreadnum * sizeof(HANDLE)); // 必须是初始化 maxThreadnum 个 sizof(HANDLE) 大小的空间

        pool->ThreadWorkersID = (DWORD *)malloc(maxThreadnum * sizeof(DWORD));
        if (pool->ThreadWorkersID == nullptr)
        {
            printf("Failed to allocate memory to ThreadWorkerID !\n");
            break;
        }
        pool->MinThreadnum = minThreadnum;
        pool->MaxThreadnum = maxThreadnum;
        pool->IsDestoryPool = 0;
        pool->WorkingThreadnum = 0;
        pool->liveThreadnum = minThreadnum; // 刚开始按照最小个数创建
        pool->NeedtoKillnum = 0;

        pool->Taskmax = taskmax;
        pool->CurrnetTasknum = 0;
        pool->front = 0;
        pool->rear = 0;
        pool->TaskQueue = (Task *)malloc(pool->Taskmax * sizeof(Task));

        InitializeCriticalSection(&pool->mutexPool); // 没有返回值
        InitializeCriticalSection(&pool->mutexWorkingnum);
        InitializeConditionVariable(&pool->isTaskEmpty);
        InitializeConditionVariable(&pool->isTaskFull);

        pool->ThreadManger = CreateThread(NULL, 0, ThreadMangerFunction, pool, 0, &(pool->ThreadMangerID));
        for (int i = 0; i < minThreadnum; i++)
        {
            // 为每个工作线程分配一个唯一的线程标识符
            /*
            之所以传入 pool 作为参数，原因是工作线程的主函数需要访问和操作线程池
            的状态，例如从任务队列中取出任务，或者调整线程的数量等。通过将 pool
            作为参数传递给工作线程的主函数，这个函数就可以访问和操作线程池的状态。
            而每个 Task 结构体中的函数指针和参数，只是单个任务的执行函数和参数，
            它们并不能访问和操作线程池的状态。因此，需要将 pool 作为参数传递给线
            程函数。

            例如，工作线程的主函数可能类似于以下的形式:
            DWORD WINAPI MyThreadFunction(LPVOID lpParam) {
                Cthreadpool* pool = (Cthreadpool*)lpParam;
                while (1) {
                    // 获取任务队列的锁
                    EnterCriticalSection(&pool->mutexPool);
                    // 从任务队列中取出任务
                    Task* task = getTask(pool);
                    // 释放任务队列的锁
                    LeaveCriticalSection(&pool->mutexPool);
                    // 执行任务
                    task->functionptr(task->funcargs);
                }
            return 0;
            }

            在这个函数中，首先将传入的参数 lpParam 转换为 Cthreadpool* 类型，然后
            在一个无限循环中，不断从任务队列中取出任务并执行。这就是为什么我们需要
            将 pool 作为参数传递给 CreateThread 函数的原因。
            */
            pool->ThreadWorkers[i] = CreateThread(NULL, 0, ThreadWokerFunction, pool, 0, &(pool->ThreadWorkersID[i]));
        }

        return pool;

    } while (0);

    /*
    在释放这些动态分配的内存之前，需要确保这些指针是有效的。如果 pool 是 NULL，
    那么 pool->ThreadWorker 和 pool->TaskQueue 就无法访问，这会导致未定义的
    行为，可能会引发程序崩溃。

    因此，在释放 pool->ThreadWorker 和 pool->TaskQueue 之前，需要先检查
    pool 是否为 NULL。这就是为什么需要使用 pool && pool->ThreadWorker 和
    pool && pool->TaskQueue 来进行判断，而不是单纯地判断 pool->ThreadWorker
    和 pool->TaskQueue。

    这样的判断可以确保只有当 pool、pool->ThreadWorker 和 pool->TaskQueue 都
    不为 NULL 时，才会尝试释放 pool->ThreadWorker 和 pool->TaskQueue 指向的
    内存。这是一种防止访问无效指针和释放未分配的内存的常见技巧。
    */
    if (pool && pool->ThreadWorkers)
    {
        free(pool->ThreadWorkers);
    }
    if (pool && pool->TaskQueue)
    {
        free(pool->TaskQueue);
    }
    if (pool)
        free(pool);

    return NULL;
}

DWORD ThreadWokerFunction(LPVOID laParam)
{
    Cthreadpool *pool = (Cthreadpool *)laParam;
    while (1)
    {
        EnterCriticalSection(&pool->mutexPool);
        // 判断队列是否为空
        /*
        写 while 循环的目的是，工作线程被唤醒后去消费任务，消费完后导致任务队列又为空
        ，其他线程判定为空后又继续阻塞在这里

        使用while循环的原因是，当线程被唤醒并重新获取临界区后，任务队列可能仍然为空，
        或者线程池可能已经被销毁。这是因为WakeConditionVariable或WakeAllConditionVariable
        函数可能被其他线程调用，而这些线程可能并没有添加新的任务到任务队列，或者可能在
        唤醒线程后销毁了线程池。因此，线程被唤醒后需要再次检查条件，如果条件仍然不满足
        ，那么就需要再次阻塞并等待。这就是为什么需要使用while循环的原因。
        */
        while (pool->CurrnetTasknum == 0 && !pool->IsDestoryPool)
        {
            // 阻塞工作线程
            SleepConditionVariableCS(&(pool->isTaskEmpty), &(pool->mutexPool), INFINITE);

            // 判断被唤醒的线程是否需要被销毁,NeedtoKillnum在管理者线程中进行设置
            if (pool->NeedtoKillnum > 0)
            {
                /*
                pool->NeedtoKillnum-- 不能放倒kill线程的if判断中。因为假如管理者线程唤醒了
                两次工作线程，但 “pool->liveThreadnum>pool->MinThreadnum” 条件一直没有成立
                此时 NeedtoKillnum=2 ，该情况下生产者往任务队列中添加新任务唤醒了工作线程，工
                作线程没有执行任务却自杀，这种情况是不符合预期的。因此管理者者线程唤醒两次工作
                线程，不管工作线程有没有自杀，都需要将 NeedtoKillnum 恢复为0。
                */
                pool->NeedtoKillnum--;
                if (pool->liveThreadnum > pool->MinThreadnum)
                {
                    pool->liveThreadnum--;
                    /*
                    在Windows API中，当一个线程拥有一个临界区（也就是获取了一个临界区的锁）时，
                    如果这个线程结束，那么这个临界区将保持被锁定的状态，其他线程将无法获取这个
                    临界区。因此，如果一个线程在结束前拥有一个临界区，那么它应该在结束前释放这
                    个临界区。

                    在代码中，SleepConditionVariableCS() 这行代码会在阻塞线程前自动释放 mutexPool
                    临界区，然后在唤醒线程后自动获取 mutexPool 临界区。因此，当线程被唤醒后，
                    它是拥有mutexPool临界区的。所以，如果在这个线程结束前调用 ExitThread(0)
                    ，应该先调用 LeaveCriticalSection() 来释放 mutexPool 临界区。
                    */
                    LeaveCriticalSection(&pool->mutexPool);
                    goto Exit;
                    // threadExit(pool);
                }
            }
        }
        // 判断线程池是否被关闭
        if (pool->IsDestoryPool)
        {
            LeaveCriticalSection(&pool->mutexPool);
            break;
        }
        Task task;
        task.functionptr = pool->TaskQueue[pool->front].functionptr;
        task.funcargs = pool->TaskQueue[pool->front].funcargs;
        pool->front = (pool->front + 1) % pool->Taskmax;
        pool->CurrnetTasknum--;

        // 工作线程已经消费了一个任务，通知生产线程可以继续添加任务
        WakeConditionVariable(&pool->isTaskFull);
        LeaveCriticalSection(&pool->mutexPool);

        /*
        线程开始工作前需要将线程池中的工作线程数+1，但其他线程也有可能会开始工作，所以
        需要对工作线程数上锁。线程工作结束后，工作线程数-1也是同理

        对于可不可以将两次加锁释放锁的操作合并为一次的问题：

            想法是可以的，但是在实际的多线程编程中，通常尽量减少锁的持有时间。因为当一个线程
            持有一个锁的时候，其他需要这个锁的线程就必须等待，这会降低并发性能。

            在该例子中，如果在执行任务和释放参数内存空间的过程中持有锁，那么这段时间内，其他
            需要修改WorkingThreadnum的线程就必须等待。而执行任务和释放内存可能是一项耗时的
            操作，这就可能导致其他线程长时间等待，降低了系统的并发性能。

            因此，虽然多次加锁和释放锁会有一些开销，但是这样可以减少锁的持有时间，提高并发性
            能。这是一种权衡，需要根据具体的应用场景和性能需求来决定。
        */
        printf("thread %ld starts working\n", GetCurrentThreadId());
        EnterCriticalSection(&pool->mutexWorkingnum);
        pool->WorkingThreadnum++;
        LeaveCriticalSection(&pool->mutexWorkingnum);
        task.functionptr(task.funcargs); // 或者写为：(*task.functionptr)(task.funcargs)
        free(task.funcargs);             // 之后初始化一块堆内存，栈内存还要保证让其不被释放
        task.funcargs = NULL;
        printf("thread %ld end working\n", GetCurrentThreadId());
        EnterCriticalSection(&pool->mutexWorkingnum);
        pool->WorkingThreadnum--;
        LeaveCriticalSection(&pool->mutexWorkingnum);
    }
Exit:
    threadExit(pool);
}

/*
管理者线程按照一定的频率对线程池进行检测，看是否需要对工作线程的个数进行调节
*/
DWORD ThreadMangerFunction(LPVOID laParam)
{
    Cthreadpool *pool = (Cthreadpool *)laParam;

    while (!pool->IsDestoryPool)
    {
        /*
        在Windows环境下编程时，通常会使用Windows API的Sleep()函数，
        它的参数是以毫秒为单位的。这是Windows特有的函数，不是标准C
        或C++库的一部分。

        另一方面，sleep()函数是POSIX标准的一部分，它的参数是以秒为
        单位的。这个函数在Windows环境下可能不可用，除非使用了某种
        POSIX兼容层，如Cygwin。

        所以，如果在Windows环境下编程，通常会使用Sleep()。如果在
        POSIX 兼容的环境（如Linux或Mac OS）下编程，则使用sleep()。
        如果希望你的代码能在多种环境下运行，可能需要根据环境使用条
        件编译来选择使用哪个函数
        */
        Sleep(3000); // 每隔3秒检测一次
        EnterCriticalSection(&pool->mutexPool);
        // 如果没有任务，就让管理者线程休眠
        if (pool->CurrnetTasknum == 0)
        {
            SleepConditionVariableCS(&(pool->isTaskEmpty), &(pool->mutexPool), INFINITE);
        }
        // 取出当前任务数量和存活线程数量
        EnterCriticalSection(&pool->mutexPool);
        int CurrentTasknum = pool->CurrnetTasknum;
        int liveThreadnum = pool->liveThreadnum;
        LeaveCriticalSection(&pool->mutexPool);

        // 取出工作线程数
        /*
        按理说简单的写法就是把工作线程数的取值放到上面与任务数量和存活
        线程数一同取值即可。因为上面是通过 mutexPool 保护了整个线程池，
        而 WorkingThreadnum是线程池中的一个成员

        之所以重新加锁是因为 WorkingThreadnum是线程池中的一个经常被访
        问的成员，为了提高操作效率，因此单独给其配一把 mutexWorkingnum
        锁。这样只需锁该变量，而不用对整个线程池上锁
        */
        EnterCriticalSection(&pool->mutexWorkingnum);
        int WorkingThreadnum = pool->WorkingThreadnum;
        LeaveCriticalSection(&pool->mutexWorkingnum);

        // 添加线程

        if (CurrentTasknum > liveThreadnum && liveThreadnum < pool->MaxThreadnum)
        {
            printf("Prepare to add threads\n");
            EnterCriticalSection(&pool->mutexPool);
            int cnt = 0; // 每添加线程后将cnt +1，当其等于 addThreadnum 后便不再添加
            for (int i = 0; i < pool->MaxThreadnum && cnt < PerThreadnum && pool->liveThreadnum < pool->MaxThreadnum; i++)
            {
                /*循环添加线程的过程中，存活线程数有可能会大于最大线程数*/

                /*在Windows API中，线程句柄是一个指向线程对象的指针，它的值在创建线
                程时由系统分配。如果线程句柄为空，那么它的值应该是NULL*/
                if (pool->ThreadWorkers[i] == NULL)
                {
                    pool->ThreadWorkers[i] = CreateThread(NULL, 0, ThreadWokerFunction, pool, 0, &pool->ThreadWorkersID[i]);
                    cnt++;
                    pool->liveThreadnum++;
                }
            }
            LeaveCriticalSection(&pool->mutexPool);
        }

        // 销毁线程
        if (WorkingThreadnum * 2 < liveThreadnum && liveThreadnum > pool->MinThreadnum)
        {
            if (WorkingThreadnum < 0)
                break;
            printf("Prepare to destroy thread\n");
            EnterCriticalSection(&pool->mutexPool);
            pool->NeedtoKillnum = PerThreadnum;
            LeaveCriticalSection(&pool->mutexPool);
            // 让工作的线程自杀
            /*管理者线程不能直接杀死空闲线程，因为它不知道谁是空闲线程，只能通知空闲线程让它自杀*/
            for (int i = 0; i < PerThreadnum; i++)
            {
                /*每次使用 WakeConditionVariable() 唤醒一个和一次使用 WakeAllConditionVariable()
                唤醒多个的效果是一样的。因为唤醒多个线程后，线程之间回去争抢 mutexPool ，换多个只有
                一个可以抢到锁，也只有一个线程可以向下执行*/
                WakeConditionVariable(&pool->isTaskEmpty);
            }
        }
        LeaveCriticalSection(&pool->mutexPool);
    }
}
// 线程退出函数
void threadExit(Cthreadpool *pool)
{
    // 获取当前执行到这个函数的线程的id
    DWORD threadID = GetCurrentThreadId();
    // 遍历工作线程数组，查找执行这个函数的线程id，若匹配则kill掉
    for (int i = 0; i < pool->MaxThreadnum; i++)
    {
        if (pool->ThreadWorkers[i] != NULL && GetThreadId(pool->ThreadWorkers[i]) == threadID)
        {
            CloseHandle(pool->ThreadWorkers[i]);
            pool->ThreadWorkers[i] = NULL;
            printf("thread: %ld exiting... \n", threadID);
            break;
        }
    }
    ExitThread(0);
}

// 销毁线程池
int destoryThreadPool(Cthreadpool *pool)
{
    printf("Destroying thread pool\n");
    if (pool == nullptr)
    {
        return 0;
    }
    // 关闭线程池
    pool->IsDestoryPool = 1;
    // 唤醒所有的工作线程和管理者线程
    WakeAllConditionVariable(&pool->isTaskEmpty);

    // 等待工作线程执行结束后自毁
    WaitForMultipleObjects(pool->liveThreadnum, pool->ThreadWorkers, TRUE, INFINITE);

    // 关闭所有线程的句柄
    for (int i = 0; i < pool->liveThreadnum; i++)
    {
        CloseHandle(pool->ThreadWorkers[i]);
    }
    CloseHandle(pool->ThreadManger);
    // 释放堆内存
    if (pool->TaskQueue)
    {
        free(pool->TaskQueue);
    }
    if (pool->ThreadWorkers)
    {
        free(pool->ThreadWorkers);
    }
    if (pool->ThreadWorkersID)
    {
        free(pool->ThreadWorkersID);
    }

    // 释放互斥锁
    DeleteCriticalSection(&pool->mutexPool);
    DeleteCriticalSection(&pool->mutexWorkingnum);
    /*
    在 Windows API 中，条件变量不需要显式销毁。不同于互斥锁（如 `CRITICAL_SECTION`），
    条件变量不关联任何系统资源或内存，它们是轻量级的对象。当你不再需要条件变量时，简单地
    不使用它就可以了。没有 `DeleteConditionVariable()` 或类似的函数是因为 Windows API
    设计条件变量时，并没有为其分配资源，因此也不需要释放资源。
    */
    free(pool);
    pool = NULL;
    return 1;
}

// 添加任务
void addTask(Cthreadpool *pool, void (*funcptr)(void *), void *funcarg)
{
    printf("Adding task to queue\n");
    EnterCriticalSection(&pool->mutexPool);
    // 任务队列满载则阻塞生产者线程
    while (pool->CurrnetTasknum == pool->Taskmax && !pool->IsDestoryPool)
    {
        SleepConditionVariableCS(&pool->isTaskFull, &pool->mutexPool, INFINITE);
    }
    if (pool->IsDestoryPool)
    {
        LeaveCriticalSection(&pool->mutexPool);
        return;
    }
    // 无需堆满检查，因为 while 循环中已经保证了队列有空位。
    pool->TaskQueue[pool->rear].functionptr = funcptr;
    pool->TaskQueue[pool->rear].funcargs = funcarg;
    pool->CurrnetTasknum++;
    pool->rear = (pool->rear + 1) % pool->Taskmax;

    // 生产者线程添加了任务，唤醒工作线程去消费。同时，也会唤醒因任务数为空而阻塞的管理者线程
    WakeConditionVariable(&pool->isTaskEmpty);
    LeaveCriticalSection(&pool->mutexPool);
}

// 获取工作的线程个数
int getWorkingThreadnum(Cthreadpool *pool)
{
    EnterCriticalSection(&pool->mutexPool);
    int WorkingThreadnum = pool->WorkingThreadnum;
    LeaveCriticalSection(&pool->mutexPool);
    return WorkingThreadnum;
}

// 获取存活线程数
int getLiveThreadnum(Cthreadpool *pool)
{
    EnterCriticalSection(&pool->mutexPool);
    int LiveThreadnum = pool->liveThreadnum;
    LeaveCriticalSection(&pool->mutexPool);
    return LiveThreadnum;
}

void taskFunc(void *arg)
{
    int num = *(int *)arg;
    printf("thread %ld is doing something....,num=%d\n", GetCurrentThreadId(), num);
    sleep(1);
}

int main()
{
    Cthreadpool *pool = createThreadPool(3, 10, 100);

    for (int i = 0; i < 100; ++i)
    {
        int *num = (int *)malloc(sizeof(int));
        *num = i + 100;
        addTask(pool, taskFunc, num);
    }
    // 销毁线程之前，工作线程有可能没有执行完毕，因此让主线程睡眠一段时间

    sleep(30);
    destoryThreadPool(pool);
    printf("shutdown ThreadPool safely\n");
    return 0;
}