#include "kernel/types.h"
#include "user.h"
#include "kernel/riscv.h"

// ---------------------------- Thread Library ----------------------------

struct thread *threads[MAX_THREADS];

struct thread *current_thread = NULL;
uint8 tid = 0;
uint8 all_finished = 0;

void thread_wrapper(struct thread *t) {
    if (t->arg) {
        t->result = (*(t->func))(t->arg);
    } else {
        t->result = t->func(0);
    }

    t->state = ZOMBIE;
    // Free allocated memory in thread

    if (t->tid != 0)
        tyield(); // Yield the current thread since it has finished
}

void initialize_context(struct context *ctx, struct thread *t, uint64 stack_base, uint32 stacksize) {
    ctx->ra = (uint64) thread_wrapper; // Set the return address to the start of the wrapper function
    ctx->sp = stack_base + stacksize - 2 * sizeof(uint64); // Set the stack pointer to the top of the stack, leaving space for the argument and a null return address

    // Initialize callee-saved registers to zero
    ctx->s0 = 0;
    ctx->s1 = 0;
    ctx->s2 = 0;
    ctx->s3 = 0;
    ctx->s4 = 0;
    ctx->s5 = 0;
    ctx->s6 = 0;
    ctx->s7 = 0;
    ctx->s8 = 0;
    ctx->s9 = 0;
    ctx->s10 = 0;
    ctx->s11 = 0;

    // Store the argument and a null return address on the stack
    uint64 *stack = (uint64 *)ctx->sp;
    stack[0] = (uint64)t;
    stack[1] = 0; // Null return address
}


// Thread library functions --------------------------------------------------
// Scheduler
void tsched()
{
    int found_new = 0;
    printf("tsched: scheduling threads\n");
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == NULL || threads[i]->tid == current_thread->tid)
            continue;
        struct thread *t = threads[i];
        printf("tsched: checking thread %d\n", t->tid);
        if (t->state == RUNNABLE)
        {
            acquire(t->lock);
            t->state = RUNNING;
            tswtch(&current_thread->tcontext, &t->tcontext);
            release(t->lock);
            found_new = 1;
        }
        current_thread = t;
        break;
    }

    if (found_new == 0)
    {
        printf("tsched: no new thread found, returning to main thread\n");
        struct thread *main_thread = threads[0];
        tswtch(&current_thread->tcontext, &main_thread->tcontext);
        current_thread = main_thread;
        thread_wrapper(main_thread);
        all_finished = 1;
    } else {
        thread_wrapper(current_thread);
    }
}

// Create a new thread
void tcreate(struct thread **thread, struct thread_attr *attr, void *(*func)(void *arg), void *arg) {
    // Allocate memory for the new thread struct
    printf("tcreate: allocating memory for thread struct\n");
    *thread = (struct thread *)malloc(sizeof(struct thread));

    // Initialize the thread's lock
    (*thread)->lock = (struct lock *)malloc(sizeof(struct lock));
    initlock((*thread)->lock, "Thread lock");

    // Set the thread's ID
    printf("tcreate: setting thread ID\n");
    (*thread)->tid = tid++;
    printf("given id: %d\n", (*thread)->tid);

    if (threads[0] == NULL)
    {  
        // Main thread
        printf("tcreate: main thread\n");
        threads[0] = *thread;
        // Main scheduling itself
        (*thread)->state = RUNNING;
        current_thread = threads[0];
        tswtch(&current_thread->tcontext, &current_thread->tcontext);
    } else {
        printf("tcreate: not main thread\n");
        for (int i = 1; i < MAX_THREADS; i++)
        {
            if (threads[i] == NULL && 
            !(threads[i]->state == RUNNING || threads[i]->state == RUNNABLE))
            {
                printf("tcreate: found available thread slot\n");
                threads[i] = *thread;
                (*thread)->state = RUNNABLE;
                break;
            }
        }
    }

    // Set default values for stacksize and res_size if not provided
    uint32 stacksize = (attr && attr->stacksize) ? attr->stacksize : PGSIZE;
    uint32 res_size = (attr && attr->res_size) ? attr->res_size : 0;
    // Free the attributes if they were allocated
    if (attr)
        free(attr);
    
    // Allocate memory for the thread's attributes
    struct thread_attr *tAttr = (struct thread_attr *)malloc(sizeof(attr));
    tAttr->stacksize = stacksize;
    tAttr->res_size = res_size;
    printf("tcreate: setting thread attributes\n");
    (*thread)->attr = tAttr;

    // Allocate memory for the thread's stack
    uint64 stack_base = (uint64)malloc(stacksize);

    // Initialize the thread's context
    printf("tcreate: initializing thread context\n");
    initialize_context(&((*thread)->tcontext), *thread, stack_base, stacksize);

    // Set the thread's function and argument
    printf("tcreate: setting thread function and argument\n");
    (*thread)->func = func;
    printf("tcreate: setting thread argument\n");
    if (arg != 0) {
        printf("tcreate: arg is not null\n");
        (*thread)->arg = arg;
    } else {
        printf("tcreate: arg is null\n");
        (*thread)->arg = NULL;
    }
    printf("tcreate: thread created\n");
    if ((*thread)->tid == 0) {
        tyield();
    }
}

// Wait for a thread to finish, and copy the result to the memory, status points to (if status and size are non-zero)
int tjoin(int tid, void *status, uint size)
{
    // TODO: Wait for the thread with TID to finish. If status and size are non-zero,
    // copy the result of the thread to the memory, status points to. Copy size bytes.
    struct thread *t = NULL;
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] != NULL && threads[i]->tid == tid)
        {
            t = threads[i];
            break;
        }
    }
    if (t == NULL)
    {
        printf("tjoin: thread with tid %d not found\n", tid);
        return -1;
    }
    
    if (status != NULL && size > 0)
    {
        printf("tjoin: copying result to memory\n");
        memcpy(status, t->result, size);
    }
    return 0;
}


// Yield the current thread
void tyield()
{
    
    // Check if the current thread is valid
    printf("tyield: yielding current thread with tid %d\n", current_thread->tid);

    // If the current thread is not finished (not in the ZOMBIE state), set it to RUNNABLE
    printf("tyield: setting current thread state to RUNNABLE\n");
    if (current_thread == NULL) {
        printf("tyield: current thread is null\n");
        return;
    }
    
    if (current_thread->state != ZOMBIE) {
        //acquire(current_thread->lock);
        current_thread->state = RUNNABLE;
        //release(current_thread->lock);
    }
    printf("tyield: releasing current thread lock\n");
    

    // Call the scheduler to switch to the next runnable thread
    printf("tyield: calling tsched()\n");
    tsched();
}

// Exit the current thread
uint8 twhoami()
{
    // TODO: Returns the thread id of the current thread
    return current_thread->tid;
}
