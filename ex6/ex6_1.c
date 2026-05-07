/*
 * RTOS-06 Exercise – Part 1 & 2
 * Creates 10 child processes with different priorities.
 * Each child performs CPU-bound computation (no blocking calls).
 * Measures and reports execution time as a function of priority.
 *
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

#define NUM_PROCESSES   10
#define WORKLOAD        50000000UL   // 50M in unsigned long literal

/* Priorities assigned to child processes (low → high) */
static const int priorities[NUM_PROCESSES] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};

/* Shared memory layout – one record per child */
typedef struct {
    int    priority;
    double elapsed_sec;
} Result;

static volatile unsigned long sink = 0;
// prevent optimisation away from removing CPU work loop
// without this compiler can consider that the operation do 
// nothing because not assigning any value so it can think that it isn't used

static void cpu_work(unsigned long n)
{
    unsigned long i, acc = 0;
    for (i = 0; i < n; ++i) {
        acc += i * i + (i ^ 0xDEADBEEFUL);
    }
    sink = acc; //prevent opti
}

int main(void)
{
    const char *shm_name = "/rtos06_prio_shm";
    int    shm_fd;
    Result *results;
    pid_t   pids[NUM_PROCESSES];
    int     i, status;

    //create shared memory for result
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (shm_fd == -1) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    //resize the shared memory to a specific byte size (size == 0 without)
    if (ftruncate(shm_fd, sizeof(Result) * NUM_PROCESSES) == -1) {
        perror("ftruncate");
        shm_unlink(shm_name);
        return EXIT_FAILURE;
    }
    //maps the shared memory to process memory
    results = (Result *)mmap(NULL, sizeof(Result) * NUM_PROCESSES, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (results == MAP_FAILED) {
        perror("mmap");
        shm_unlink(shm_name); //remove the shared memory
        return EXIT_FAILURE;
    }

    printf("=== RTOS-06: Priority vs Execution Time ===\n");
    printf("Spawning %d child processes...\n\n", NUM_PROCESSES);

    for (i = 0; i < NUM_PROCESSES; ++i) {
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            shm_unlink(shm_name);
            return EXIT_FAILURE;
        }

        if (pids[i] == 0) {
            struct sched_param sp;
            struct timeval     t_start, t_end;

            // map shared memory in child 
            int cfd = shm_open(shm_name, O_RDWR, 0600);
            Result *cres = (Result *)mmap(NULL, sizeof(Result) * NUM_PROCESSES, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);

            // Set Sporadic scheduling QNX default + priority
            memset(&sp, 0, sizeof(sp));
            sp.sched_priority = priorities[i];

            if (sched_setscheduler(0, SCHED_SPORADIC, &sp) == -1) {
                // if not work then set param
                sched_setparam(0, &sp);
            }

            // Measure pure CPU time without opti
            gettimeofday(&t_start, NULL);
            cpu_work(WORKLOAD);
            gettimeofday(&t_end, NULL);

            double elapsed = (t_end.tv_sec  - t_start.tv_sec) + (t_end.tv_usec - t_start.tv_usec) * 1e-6;

            cres[i].priority    = priorities[i];
            cres[i].elapsed_sec = elapsed;

            munmap(cres, sizeof(Result) * NUM_PROCESSES); //unmap when done
            close(cfd);
            _exit(EXIT_SUCCESS);
        }
    }

    for (i = 0; i < NUM_PROCESSES; ++i) {
        waitpid(pids[i], &status, 0);
    }

    for (i = 0; i < NUM_PROCESSES; ++i) {
        printf("Priority %d: %.6f seconds\n", results[i].priority, results[i].elapsed_sec);
    }

    // Higher-priority processes may finish faster because the scheduler pre-empts lower-priority ones to run them

    //clean the shared memory
    munmap(results, sizeof(Result) * NUM_PROCESSES);
    close(shm_fd);
    shm_unlink(shm_name);

    return EXIT_SUCCESS;
}