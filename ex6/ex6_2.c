/*
 * RTOS-06 Exercise – Part 3 & 4  (3 processes per policy)
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

#define PROCS_PER_POLICY  3
#define NUM_POLICIES      3
#define NUM_CHILDREN      (NUM_POLICIES * PROCS_PER_POLICY)   /* 9 */
#define FIXED_PRIORITY    20
#define WORKLOAD          50000000UL

static const int   policies[NUM_POLICIES]     = { SCHED_FIFO, SCHED_RR, SCHED_SPORADIC };
static const char *policy_names[NUM_POLICIES] = { "SCHED_FIFO", "SCHED_RR", "SCHED_SPORADIC" };

typedef struct {
    int    policy_idx;
    int    instance;
    pid_t  pid;
    int    start_rank;
    int    finish_rank;
    double elapsed_sec;
    int    actual_policy;
} Result;

typedef struct {
    sem_t  gate_sem;
    sem_t  ready_sem;
    int    start_counter;
    int    finish_counter;
} Gate;

static volatile unsigned long sink = 0;

static void cpu_work(unsigned long n)
{
    unsigned long i, acc = 0;
    for (i = 0; i < n; ++i)
        acc += i * i + (i ^ 0xCAFEBABEUL);
    sink = acc;
}

int main(void)
{
    const char *shm_results_name = "/rtos06_sched_results";
    const char *shm_gate_name    = "/rtos06_sched_gate";

    int     res_fd, gate_fd;
    Result *results;
    Gate   *gate;
    pid_t   pids[NUM_CHILDREN];
    int     i, status;

    res_fd = shm_open(shm_results_name, O_CREAT | O_RDWR, 0600);
    if (res_fd == -1) { perror("shm_open results"); return EXIT_FAILURE; }
    if (ftruncate(res_fd, sizeof(Result) * NUM_CHILDREN) == -1) {
        perror("ftruncate results"); shm_unlink(shm_results_name); return EXIT_FAILURE;
    }
    results = mmap(NULL, sizeof(Result) * NUM_CHILDREN,
                   PROT_READ | PROT_WRITE, MAP_SHARED, res_fd, 0);
    if (results == MAP_FAILED) {
        perror("mmap results"); shm_unlink(shm_results_name); return EXIT_FAILURE;
    }

    gate_fd = shm_open(shm_gate_name, O_CREAT | O_RDWR, 0600);
    if (gate_fd == -1) { perror("shm_open gate"); return EXIT_FAILURE; }
    if (ftruncate(gate_fd, sizeof(Gate)) == -1) {
        perror("ftruncate gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }
    gate = mmap(NULL, sizeof(Gate), PROT_READ | PROT_WRITE, MAP_SHARED, gate_fd, 0);
    if (gate == MAP_FAILED) {
        perror("mmap gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }

    if (sem_init(&gate->gate_sem,  1, 0) == -1) { perror("sem_init gate");  return EXIT_FAILURE; }
    if (sem_init(&gate->ready_sem, 1, 0) == -1) { perror("sem_init ready"); return EXIT_FAILURE; }
    gate->start_counter  = 0;
    gate->finish_counter = 0;

    for (i = 0; i < NUM_CHILDREN; ++i) {
        int pol_idx  = i / PROCS_PER_POLICY;
        int instance = i % PROCS_PER_POLICY;

        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return EXIT_FAILURE; }

        if (pids[i] == 0) {
            struct sched_param sp;
            struct timeval     t_start, t_end;

            int     cres_fd  = shm_open(shm_results_name, O_RDWR, 0600);
            int     cgate_fd = shm_open(shm_gate_name,    O_RDWR, 0600);
            Result *cres  = mmap(NULL, sizeof(Result) * NUM_CHILDREN, PROT_READ | PROT_WRITE, MAP_SHARED, cres_fd, 0);
            Gate   *cgate = mmap(NULL, sizeof(Gate), PROT_READ | PROT_WRITE, MAP_SHARED, cgate_fd, 0);

            memset(&sp, 0, sizeof(sp));
            sp.sched_priority = FIXED_PRIORITY;
            if (sched_setscheduler(0, policies[pol_idx], &sp) == -1) {
                printf("[child %d] sched_setscheduler(%s) failed: %s\n",
                        i, policy_names[pol_idx], strerror(errno));
            }

            cres[i].policy_idx    = pol_idx;
            cres[i].instance      = instance;
            cres[i].pid           = getpid();
            cres[i].actual_policy = sched_getscheduler(0);

            sem_post(&cgate->ready_sem);
            if (sem_wait(&cgate->gate_sem) == -1) {
            	perror("sem_wait");
            	exit(EXIT_FAILURE);
            }

            cres[i].start_rank = __sync_fetch_and_add(&cgate->start_counter, 1) + 1;

            gettimeofday(&t_start, NULL);
            cpu_work(WORKLOAD);
            gettimeofday(&t_end, NULL);

            cres[i].finish_rank  = __sync_fetch_and_add(&cgate->finish_counter, 1) + 1;
            cres[i].elapsed_sec  = (t_end.tv_sec  - t_start.tv_sec)
                                 + (t_end.tv_usec - t_start.tv_usec) * 1e-6;

            printf("DONE  [%s :%d]  pid=%d  start=%d  finish=%d  %.6f s\n",
                   policy_names[pol_idx], instance, (int)getpid(),
                   cres[i].start_rank, cres[i].finish_rank, cres[i].elapsed_sec);

            munmap(cres,  sizeof(Result) * NUM_CHILDREN);
            munmap(cgate, sizeof(Gate));
            close(cres_fd);
            close(cgate_fd);
            exit(EXIT_SUCCESS);
        }
    }

    printf("Waiting for all %d children to be ready...\n", NUM_CHILDREN);
    for (i = 0; i < NUM_CHILDREN; ++i)
        sem_wait(&gate->ready_sem);

    printf("Launching all childs\n");

    for (i = 0; i < NUM_CHILDREN; ++i)
        sem_post(&gate->gate_sem);

    for (i = 0; i < NUM_CHILDREN; ++i)
        waitpid(pids[i], &status, 0);

    /* Cleanup */
    munmap(results, sizeof(Result) * NUM_CHILDREN);
    munmap(gate,    sizeof(Gate));
    close(res_fd);
    close(gate_fd);
    sem_destroy(&gate->gate_sem);
    sem_destroy(&gate->ready_sem);
    shm_unlink(shm_results_name);
    shm_unlink(shm_gate_name);

    return EXIT_SUCCESS;
}
