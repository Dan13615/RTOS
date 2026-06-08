/*
 * RTOS-06 Exercise - Part 1 and 2
 *
 * Fork 10 children with priorities 10..100. Shared memory holds the per-child
 * results; a shared gate semaphore releases every child at the same instant.
 * The parent runs at a priority above every child, so it can post all gate
 * semaphores before any child runs, then it blocks in waitpid and the children
 * contend. The meaningful result is the finish order, not the elapsed time,
 * because every child runs the same loop. An atomic counter records the order.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>

#define NUM_PROCESSES    10
#define PARENT_PRIORITY  110            /* above the highest child priority */
#define WORKLOAD         5000000000ULL  /* unsigned long long, no overflow */

static const int priorities[NUM_PROCESSES] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};

/* Per-child result written into shared memory */
typedef struct {
    int    priority;
    int    finish_rank;     /* order in which the child finished */
    double elapsed_sec;
} Result;

/* Shared sync block: gate releases children, ready counts set-up children */
typedef struct {
    sem_t  gate_sem;
    sem_t  ready_sem;
    int    finish_counter;  /* atomic, hands out finish ranks */
} Gate;

/* volatile sink: stops the compiler from optimising the work loop away */
static volatile unsigned long long sink = 0;

/* Pure CPU loop, no blocking calls */
static void cpu_work(unsigned long long n)
{
    unsigned long long i, acc = 0;
    for (i = 0; i < n; ++i)
        acc += i * i + (i ^ 0xDEADBEEFULL);
    sink = acc;
}

int main(void)
{
    const char *shm_results_name = "/rtos06_results";
    const char *shm_gate_name    = "/rtos06_gate";

    int     res_fd, gate_fd;
    Result *results;
    Gate   *gate;
    pid_t   pids[NUM_PROCESSES];
    int     i, status;

    /* shared memory: results */
    res_fd = shm_open(shm_results_name, O_CREAT | O_RDWR, 0600);
    if (res_fd == -1) { perror("shm_open results"); return EXIT_FAILURE; }
    if (ftruncate(res_fd, sizeof(Result) * NUM_PROCESSES) == -1) {
        perror("ftruncate results"); shm_unlink(shm_results_name); return EXIT_FAILURE;
    }
    results = mmap(NULL, sizeof(Result) * NUM_PROCESSES,
                   PROT_READ | PROT_WRITE, MAP_SHARED, res_fd, 0);
    if (results == MAP_FAILED) {
        perror("mmap results"); shm_unlink(shm_results_name); return EXIT_FAILURE;
    }

    /* shared memory: gate (semaphores live here) */
    gate_fd = shm_open(shm_gate_name, O_CREAT | O_RDWR, 0600);
    if (gate_fd == -1) { perror("shm_open gate"); return EXIT_FAILURE; }
    if (ftruncate(gate_fd, sizeof(Gate)) == -1) {
        perror("ftruncate gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }
    gate = mmap(NULL, sizeof(Gate), PROT_READ | PROT_WRITE, MAP_SHARED, gate_fd, 0);
    if (gate == MAP_FAILED) {
        perror("mmap gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }

    /* process-shared (pshared=1), both start at 0 so children block */
    if (sem_init(&gate->gate_sem,  1, 0u) == -1) { perror("sem_init gate");  return EXIT_FAILURE; }
    if (sem_init(&gate->ready_sem, 1, 0u) == -1) { perror("sem_init ready"); return EXIT_FAILURE; }
    gate->finish_counter = 0;

    /* Parent runs above every child, so it can post all gates before any
       child runs. Without this the first released child preempts the parent
       and runs to completion, and the priority 10 child can wait forever. */
    {
        struct sched_param psp;
        memset(&psp, 0, sizeof(psp));
        psp.sched_priority = PARENT_PRIORITY;
        if (sched_setscheduler(0, SCHED_FIFO, &psp) == -1)
            perror("sched_setscheduler parent");
    }

    /* Create the 10 children */
    for (i = 0; i < NUM_PROCESSES; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return EXIT_FAILURE; }

        if (pids[i] == 0) {
            /* child: map the same shared memory the parent created */
            struct sched_param sp;
            struct timeval     t_start, t_end;

            int     cres_fd  = shm_open(shm_results_name, O_RDWR, 0600);
            int     cgate_fd = shm_open(shm_gate_name,    O_RDWR, 0600);
            Result *cres  = mmap(NULL, sizeof(Result) * NUM_PROCESSES,
                                 PROT_READ | PROT_WRITE, MAP_SHARED, cres_fd, 0);
            Gate   *cgate = mmap(NULL, sizeof(Gate),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, cgate_fd, 0);

            /* Set this child's priority, keep a fixed policy */
            memset(&sp, 0, sizeof(sp));
            sp.sched_priority = priorities[i];
            if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1) {
                printf("[child %d] sched_setscheduler(prio=%d) failed: %s\n",
                       i, priorities[i], strerror(errno));
            }

            printf("READY  priority=%d  pid=%d\n", priorities[i], (int)getpid());

            /* Tell the parent this child is set up, then wait on the gate */
            sem_post(&cgate->ready_sem);
            if (sem_wait(&cgate->gate_sem) == -1) { perror("sem_wait"); _exit(EXIT_FAILURE); }

            /* Time the work this child does itself */
            gettimeofday(&t_start, NULL);
            cpu_work(WORKLOAD);
            gettimeofday(&t_end, NULL);

            cres[i].priority    = priorities[i];
            cres[i].finish_rank = __sync_fetch_and_add(&cgate->finish_counter, 1) + 1;
            cres[i].elapsed_sec = (t_end.tv_sec  - t_start.tv_sec)
                                + (t_end.tv_usec - t_start.tv_usec) * 1e-6;

            printf("DONE   priority=%d  finish=%d  %.6f s\n",
                   priorities[i], cres[i].finish_rank, cres[i].elapsed_sec);

            munmap(cres,  sizeof(Result) * NUM_PROCESSES);
            munmap(cgate, sizeof(Gate));
            close(cres_fd);
            close(cgate_fd);
            _exit(EXIT_SUCCESS);
        }
    }

    /* One ready_sem wait per child: blocks until all are set up and gated */
    for (i = 0; i < NUM_PROCESSES; ++i)
        sem_wait(&gate->ready_sem);

    printf("\nAll children ready, releasing them together...\n\n");

    /* One gate post per child releases them all at once */
    for (i = 0; i < NUM_PROCESSES; ++i)
        sem_post(&gate->gate_sem);

    /* Wait for all children */
    for (i = 0; i < NUM_PROCESSES; ++i)
        waitpid(pids[i], &status, 0);

    /* Report ordered by finish rank */
    printf("\n=== Results (finish order) ===\n");
    {
        int rank, k;
        for (rank = 1; rank <= NUM_PROCESSES; ++rank)
            for (k = 0; k < NUM_PROCESSES; ++k)
                if (results[k].finish_rank == rank) {
                    printf("finish %2d: priority %3d   %.6f s\n",
                           rank, results[k].priority, results[k].elapsed_sec);
                    break;
                }
    }

    /* Cleanup: destroy the semaphores before unmapping the gate memory */
    sem_destroy(&gate->gate_sem);
    sem_destroy(&gate->ready_sem);
    munmap(results, sizeof(Result) * NUM_PROCESSES);
    munmap(gate,    sizeof(Gate));
    close(res_fd);
    close(gate_fd);
    shm_unlink(shm_results_name);
    shm_unlink(shm_gate_name);

    return EXIT_SUCCESS;
}
