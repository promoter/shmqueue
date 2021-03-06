//
//  messagequeue_h
//
//  Created by 杜国超 on 17/6/22.
//  Copyright © 2017年 杜国超. All rights reserved.
//

#ifndef messagequeue_h
#define messagequeue_h

#include <iostream>
#include "shm_rwlock.h"

#define EXTRA_BYTE 8
#define MESS_SIZE_TYPE unsigned short


#define CACHELINE_SIZE 64
//修改字对齐规则，避免false sharing
#define CACHELINE_ALIGN  __attribute__((aligned(CACHELINE_SIZE)))

////内存屏障
//#define __READ_BARRIER__ \
//    __asm__ __volatile__("lfence":::"memory");
//#define __WRITE_BARRIER__ \
//    __asm__ __volatile__("sfence":::"memory");

namespace shmmqueue
{
typedef unsigned char BYTE;

enum class eQueueModel: unsigned char
{
    ONE_READ_ONE_WRITE,   //一个进程读消息一个进程写消息
    ONE_READ_MUL_WRITE,   //一个进程读消息多个进程写消息
    MUL_READ_ONE_WRITE,   //多个进程读消息一个进程写消息
    MUL_READ_MUL_WRITE,   //多个进程读消息多个进程写消息
};

enum class eQueueErrorCode
{
    QUEUE_OK = 0,     // param error
    QUEUE_PARAM_ERROR = -1,     // param error
    QUEUE_NO_SPACE = -2,        // message queue has no space
    QUEUE_NO_MESSAGE = -3,      // message queue has no message
    QUEUE_DATA_SEQUENCE_ERROR = -4,// message queue the message sequence error
};

enum class enShmModule: unsigned char
{
    SHM_INIT,     //第一次申请共享内存，初始化
    SHM_RESUME,   //共享内存已存在，恢复重新映射共享内存数据
};

class CACHELINE_ALIGN CMessageQueue
{
private:
    CMessageQueue();
    CMessageQueue(eQueueModel module, key_t shmid, size_t size);
public:
    ~CMessageQueue();
    CMessageQueue(const CMessageQueue &) = delete;
    CMessageQueue(CMessageQueue &&) = delete;
    CMessageQueue &operator=(const CMessageQueue &) = delete;
public:
    /**
     * 添加消息 对于mes queue来说是写操作，因为在队列中添加了一个消息包,仅修改m_iEnd
     * 写取共享内存管道（改变读写索引）,，读共享内存仅改变m_iEnd，保证读单进程读和写进程不会发生竞争，写不会造成数据不一致
     * @param message
     * @param length
     * @return
     */
    int SendMessage(BYTE *message, MESS_SIZE_TYPE length);
    /**
     * 获取消息 对于mes queue来说是读操作，因为从队列中拿走了一个消息包 仅修改m_iBegin
     * 读取共享内存管道（改变读写索引）,，读共享内存仅改变m_iBegin，保证读单进程读和写进程不会发生竞争，写不会造成数据不一致
     * @param pOutCode
     * @return message  > 0 data len ,or < 0 error code
     * */
    int GetMessage(BYTE *pOutCode);
    /**
     * 从mess queue 头部读取一个消息，从队列中copy走了一个消息包没有改变mess queue
     * @param pOutCode
     * @param pOutLength
     * @return message  > 0 data len ,or < 0 error code
     * */
    int ReadHeadMessage(BYTE *pOutCode);
    /**
     * 从mess queue删除头部删除一个消息，仅修改m_iBegin
     * @param iCodeOffset
     * @param pOutCode
     * @return
     * */
    int DeleteHeadMessage();
    /**
     * 打印队列信息
     * 这里没有加锁打印仅供参考，不一定是正确的
     **/
    void PrintfTrunk();
private:
    //获取消息queue在内存中的开始位置
    BYTE *QueueBeginAddr();
    //获取消息queue在内存中的结束位置
    BYTE *QueueEndAddr();
    //获取消息trunk m_iBegin在内存中的开始位置
    BYTE *MessageBeginAddr();
    //获取消息trunk m_iEnd在内存中的开始位置
    BYTE *MessageEndAddr();
    //获取空闲区大小
    unsigned int GetFreeSize();
    //获取数据长度
    unsigned int GetDataSize();
    //获取存储数据的内存取长度（空闲的和占用的）
    unsigned int GetQueueLength();
    //初始化lock
    void InitLock();
    //是否要对读端上锁
    bool IsReadLock();
    //是否要对写端上锁
    bool IsWriteLock();
public:
    //创建共享内存
    static BYTE *CreateShareMem(key_t iKey, long vSize, enShmModule &shmModule, int &shmId);
    //销毁共享内存
    static int DestroyShareMem(key_t iKey);
    //创建CMssageQueue对象
    static CMessageQueue *CreateInstance(key_t shmkey,
                                         size_t queuesize,
                                         eQueueModel queueModule = eQueueModel::MUL_READ_MUL_WRITE);
public:
    struct CACHELINE_ALIGN stMemTrunk
    {
    public:
        /**
         * 0) 单线程读单线程写模型　https://blog.csdn.net/D_Guco/article/details/100066985
         * 1) 这里读写索引用int类型,cpu可以保证对float,double和long除外的基本类型的读写是原子的,保证一个线程不会读到另外一个线程写到一半的值
         * 2) 在变量之间插入一个64位(cache line的长度)的变量(没有实际的计算意义),但是可以保证两个变量不会同时在一个cache line里,防止不同的
         *    进程或者线程同时访问在一个cache line里不同的变量产生false sharing,
         */
        unsigned int m_iBegin;
        long long __1;
        unsigned int m_iEnd;
        long long __2;
        int m_iKey;
        long long __3;
        unsigned int m_iSize;
        long long __4;
        eQueueModel m_eQueueModule;
        stMemTrunk()
        {}
        ~stMemTrunk()
        {}

        void *operator new(size_t nSize)
        {
            BYTE *pTemp;

            if (!CMessageQueue::m_pCurrAddr) {
                return (void *) NULL;
            }
            else {
                pTemp = CMessageQueue::m_pCurrAddr;
            }
            return (void *) pTemp;
        }

        void operator delete(void *pBase)
        {

        }
    };
private:
    stMemTrunk *m_stMemTrunk;
    CShmRWlock *m_pReadLock;  //m_iBegin 锁
    CShmRWlock *m_pWriteLock; //m_iEnd 锁
public:
    static BYTE *m_pCurrAddr;
};
}


#endif /* messagequeue_h */
