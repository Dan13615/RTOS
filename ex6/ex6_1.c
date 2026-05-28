/*
 * RTOS-06 Exercise – Part 1 & 2
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

#define NUM_PROCESSES   10
#define WORKLOAD        5000000000UL

static const int priorities[NUM_PROCESSES] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};

/* Per-child result */
typedef struct {
    int    priority;
    double elapsed_sec;
} Result;

typedef struct {
    sem_t sem;
} Gate;

static volatile unsigned long sink = 0;

static void cpu_work(unsigned long n)
{
    unsigned long i, acc = 0;
    for (i = 0; i < n; ++i)
        acc += i * i + (i ^ 0xDEADBEEFUL);
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

    /*shared memory: gate (semaphore lives here)*/
    gate_fd = shm_open(shm_gate_name, O_CREAT | O_RDWR, 0600);
    if (gate_fd == -1) { perror("shm_open gate"); return EXIT_FAILURE; }
    if (ftruncate(gate_fd, sizeof(Gate)) == -1) {
        perror("ftruncate gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }
    gate = mmap(NULL, sizeof(Gate), PROT_READ | PROT_WRITE, MAP_SHARED, gate_fd, 0);
    if (gate == MAP_FAILED) {
        perror("mmap gate"); shm_unlink(shm_gate_name); return EXIT_FAILURE;
    }

    if (sem_init(&gate->sem, 1, 0) == -1) {
        perror("sem_init"); return EXIT_FAILURE;
    }

    for (i = 0; i < NUM_PROCESSES; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return EXIT_FAILURE; }

        if (pids[i] == 0) {
            struct sched_param sp;
            struct timeval     t_start, t_end;

            int     cres_fd  = shm_open(shm_results_name, O_RDWR, 0600);
            int     cgate_fd = shm_open(shm_gate_name,    O_RDWR, 0600);
            Result *cres  = mmap(NULL, sizeof(Result) * NUM_PROCESSES,
                                 PROT_READ | PROT_WRITE, MAP_SHARED, cres_fd, 0);
            Gate   *cgate = mmap(NULL, sizeof(Gate),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, cgate_fd, 0);

            memset(&sp, 0, sizeof(sp));
            sp.sched_priority = priorities[i];
            if (sched_setscheduler(0, SCHED_SPORADIC, &sp) == -1)
                sched_setparam(0, &sp);

            printf("READY  priority=%d  pid=%d\n", priorities[i], getpid());

            if (sem_wait(&cgate->sem) == -1) { perror("sem_wait"); _exit(EXIT_FAILURE); }

            gettimeofday(&t_start, NULL);
            cpu_work(WORKLOAD);
            gettimeofday(&t_end, NULL);

            double elapsed = (t_end.tv_sec  - t_start.tv_sec)
                           + (t_end.tv_usec - t_start.tv_usec) * 1e-6;

            cres[i].priority    = priorities[i];
            cres[i].elapsed_sec = elapsed;

            printf("DONE   priority=%d  %.6f s\n", priorities[i], elapsed);

            munmap(cres,  sizeof(Result) * NUM_PROCESSES);
            munmap(cgate, sizeof(Gate));
            close(cres_fd);
            close(cgate_fd);
            _exit(EXIT_SUCCESS);
        }
    }

    printf("\nAll children ready releasing in 2 s...\n\n");
    sleep(2);

    for (i = 0; i < NUM_PROCESSES; ++i)
        sem_post(&gate->sem);

    /* Wait for all children */
    for (i = 0; i < NUM_PROCESSES; ++i)
        waitpid(pids[i], &status, 0);

    /* Report */
    printf("\n=== Results ===\n");
    for (i = 0; i < NUM_PROCESSES; ++i)
        printf("Priority %3d: %.6f seconds\n",
               results[i].priority, results[i].elapsed_sec);

    /* Cleanup */
    munmap(results, sizeof(Result) * NUM_PROCESSES);
    munmap(gate,    sizeof(Gate));
    close(res_fd);
    close(gate_fd);
    sem_destroy(&gate->sem);
    shm_unlink(shm_results_name);
    shm_unlink(shm_gate_name);

    return EXIT_SUCCESS;
}
