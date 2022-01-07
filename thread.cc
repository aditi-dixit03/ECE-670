#include<iostream>
#include<stdlib.h>
#include<ucontext.h>
#include<list>
#include"interrupt.h"
#include"thread.h"

using namespace std;

// #define STACK_SIZE 262144    /* size of each thread's stack */

/* #define handle_error(msg) \
    do {perror(msg); exit(EXIT_FAILURE); } while(0) */

typedef void (*thread_startfunc_t) (void *);

struct TCB{
    int threadID;
    char* stack;
    ucontext_t* context;
    int status;
};

struct Lock{
    int lockID;
    TCB* acquired;
    list<TCB*>* blockedQueue;
};

struct Condition{
    int condID;
    list<TCB*>* waitingQueue;
};

static TCB* current;
static ucontext_t* switching;
static list<TCB*> ready;
static TCB* mainThread;
static list<Lock*> lockQueue;
static list<Condition*> condQueue;
static int id = 1;
//No function can be called before thread_libinit
static int calling = 0;

static int startFunction(thread_startfunc_t func, void* arg){
	interrupt_enable();
	func(arg);
    current->status = 1;
    interrupt_disable();
    swapcontext(current->context, switching);
    return 0;
}

static int initializeMainThread(thread_startfunc_t func, void* arg){
	interrupt_disable();
	try{
        mainThread = new TCB;
        mainThread->context = new ucontext_t;
        getcontext(mainThread->context);
        mainThread->stack = new char[STACK_SIZE];
        mainThread->context->uc_stack.ss_sp = new char[STACK_SIZE];
        mainThread->context->uc_stack.ss_size = STACK_SIZE;
        mainThread->context->uc_link = NULL;
        makecontext(mainThread->context, (void(*)())startFunction, 2, func, arg);
        mainThread->status = 0;   
    }
    catch(std::bad_alloc badAlloc){
        delete mainThread->stack;
        delete mainThread->context;
        delete mainThread;
        interrupt_enable();
        return -1;
    }
	interrupt_enable();
	return -1;

}

static int initializeContext(){
	try{
        switching = new ucontext_t;
    }
    catch(std::bad_alloc badAlloc){
        delete switching;
        return -1;
    }
    return 0;
}

int thread_libinit(thread_startfunc_t func, void* arg){
	if(calling == 1){
		return -1;
	}
    calling = 1;
    //Creating the first main thread
    if(initializeMainThread(func, arg)){
    	return -1;
    }
    
    current = mainThread;
    //Initialize a switching thread
    initializeContext();
    interrupt_disable();
    getcontext(switching);
    swapcontext(switching, mainThread->context);
    while(ready.size() > 0){
        //If the thread has finished execution,
        //delete the thread.
        if(current->status == 1){
            current->context->uc_stack.ss_sp = NULL;
            current->context->uc_stack.ss_size = 0;
            delete current->stack;
            delete current->context;
            delete current;
        }
        TCB* next = ready.front();
        ready.pop_front();
        current = next;
        swapcontext(switching, current->context);
    }
    cout << "Thread library exiting.\n";
    exit(0);
}

int thread_create(thread_startfunc_t func, void* arg){
    if(calling == 0){
        return -1;
    }
    cout<<"Thread calling.\n";
    interrupt_disable();
    TCB* thread;
	try{
	    thread = new TCB;
	    thread->context = new ucontext_t;
	    thread->threadID = id++;
	    getcontext(thread->context);
	    thread->stack = new char[STACK_SIZE];
	    thread->context->uc_stack.ss_sp = new char[STACK_SIZE];
	    thread->context->uc_stack.ss_size = STACK_SIZE;
	    thread->context->uc_link = NULL;
	    makecontext(thread->context, (void(*)())startFunction, 2, func, arg);
	    thread->status = 0;
	    ready.push_back(thread);
    }
    catch(std::bad_alloc badAlloc){
        delete thread->stack;
        delete thread->context;
        delete thread;
        interrupt_enable();
        return -1;
    }
    //enable them again
    interrupt_enable();
    return 0;

}

int thread_lock(unsigned int lockID){
    if(calling == 0){
        return -1;
    }
    interrupt_disable();
    list<Lock*> :: iterator itr;
    Lock* lock;
    for(itr = lockQueue.begin(); itr != lockQueue.end(); itr++){
        if((*itr)->lockID == lockID){
            break;
        }
    }
    if(itr == lockQueue.end()){
        //Create a new lock
        try{
            lock = new Lock;
            lock->lockID = lockID;
            lock->acquired = current;
            lock->blockedQueue = new list<TCB*>;
            lockQueue.push_back(lock);   
        }
        catch(std::bad_alloc badAlloc){
            delete lock->blockedQueue;
            delete lock;
            interrupt_enable();
            return -1;
        }
    }
    else{
        //Lock already exists
        lock = (*itr);
        if(lock->acquired == NULL){
            //lock exists but is not held by any thread
            //acquire it
            lock->acquired = current;
        }
        else{
            if(lock->acquired->threadID != current->threadID){
                //Lock cannot be acquired, get in line
                lock->blockedQueue->push_back(current);
                swapcontext(current->context, switching);
            }
            else{
                //current thread is holding the lock
                //return invalid
                interrupt_enable();
                return -1;
            }
        }
    }
    interrupt_enable();
    return 0;
}

int thread_unlock(unsigned int lockID){
    if(calling == 0){
        return -1;
    }
    //The lock has to be found to be unlocked
    interrupt_disable();
    Lock* lock;
    list<Lock*> :: iterator itr;
    for(itr = lockQueue.begin(); itr != lockQueue.end(); itr++){
        if((*itr)->lockID == lockID){
            break;
        }
    }
    if(itr == lockQueue.end()){
    	interrupt_enable();
        //Lock not found
        return -1;
    }
    else{
        //Lock found
        //Take one thread out of the blockedQueue
        //Place it onto the ready queue
        lock = (*itr);
        if(lock->acquired == NULL){
            //lock is not acquired by anyone
        }else{
            if(lock->acquired->threadID != current->threadID){
            	interrupt_enable();
                //lock not held by current to be able to release it
                return -1;
            }
            else{
                //lock->acquired->lockID equals current->lockID
                //check if any threads are blocked.
                //if yes,
                if((lock->blockedQueue)->size() > 0){
                    //Now the lock is owned by the thread in the blockedQueue
                    lock->acquired = lock->blockedQueue->front();
                    ready.push_back(lock->acquired);
                    lock->blockedQueue->pop_front();
                   
                }
                else{
                    //No threads in the blockedQueue
                    lock->acquired = NULL;
                }
            }
        }
    }
    interrupt_enable();
    return 0;
}

int thread_wait(unsigned int lockID, unsigned int condID){
    if(calling == 0){
        return -1;
    }
    //Unlock the lock
    //wait on cond
    //Give the lock to the next thread

    int out = 1;
    out = thread_unlock(lockID);
    interrupt_disable();

    if(out==0){
        //Proceed ahead only if thread can be unlocked
        list<Condition*> :: iterator itr;
        Condition* condition;
        for(itr = condQueue.begin(); itr != condQueue.end(); itr++){
            if((*itr)->condID == condID){
                //found the condition
                break;
            }
        }
        if(itr == condQueue.end()){
            try{
                //Create a new condition
                condition = new Condition;
                condition->condID = condID;
                //Initialize a new wait queue
                condition->waitingQueue = new list<TCB*>;
                //push current thread into wait
                condition->waitingQueue->push_back(current);
                //push condition onto condition Queue
                //this lets us know multiple threads that are waiting onto each condVar
                condQueue.push_back(condition);
            }
            catch(std::bad_alloc badAlloc){
                delete condition->waitingQueue;
                delete condition;
                interrupt_enable();
                return -1;
            }
        }
        else{
            //Condition already exists
            condition = (*itr);
            condition->waitingQueue->push_back(current);
        }
        swapcontext(current->context, switching);
        //The lock that has been realeased has to be acquired by another thread
        interrupt_enable();
        return thread_lock(lockID);
    }
    interrupt_enable();
    return -1;
   
}

int thread_signal(unsigned int lockID, unsigned int condID){
    if(calling == 0){
        return -1;
    }
    interrupt_disable();
    list<Condition*> :: iterator itr;
    for(itr = condQueue.begin(); itr != condQueue.end(); itr++){
        if((*itr)->condID == condID){
            break;
        }
    }
    if(itr == condQueue.end()){
    	interrupt_enable();
    	//Conditional variable not found is not an error
        return 0;
    }
    else{
        TCB* thread;
        if((((*itr)->waitingQueue)->size()) > 0){
            thread = (*itr)->waitingQueue->front();
            (*itr)->waitingQueue->pop_front();
            ready.push_back(thread);
        }
    }
    interrupt_enable();
    return 0;
}

int thread_broadcast(unsigned int lockID, unsigned int condID){
    if(calling == 0){
        return -1;
    }
    interrupt_disable();
    list<Condition*> :: iterator itr;
    for(itr = condQueue.begin(); itr != condQueue.end(); itr++){
        if((*itr)->condID == condID){
            break;
        }
    }
    if(itr == condQueue.end()){
    	interrupt_enable();
        return 0;
    }
    else{
        TCB* thread;
        while((((*itr)->waitingQueue)->size()) > 0){
            thread = (*itr)->waitingQueue->front();
            (*itr)->waitingQueue->pop_front();
            ready.push_back(thread);
        }
    }
    interrupt_enable();
    return 0;
}

int thread_yield(){
    if(calling == 0){
        return -1;
    }
    interrupt_disable();
    ready.push_back(current);
    swapcontext(current->context, switching);
    interrupt_enable();
    return 0;

}
