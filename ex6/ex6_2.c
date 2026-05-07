/*
 * RTOS-06 Exercise – Part 3 & 4
 * Creates N child processes, each using a different scheduling algorithm:
 *   SCHED_FIFO, SCHED_RR (Round Robin), SCHED_SPORADIC
 * All children run at the SAME priority so the comparison is fair.
 * Each child performs CPU-bound computation (no blocking calls).
 * Measures and reports execution time as a function of scheduling policy.
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

#define NUM_POLICIES   3
#define FIXED_PRIORITY 20
#define WORKLOAD       50000000UL // 50M

// Policy table 
static const int   policies[NUM_POLICIES]      = { SCHED_FIFO, SCHED_RR, SCHED_SPORADIC };
static const char *policy_names[NUM_POLICIES]  = { "SCHED_FIFO", "SCHED_RR (Round Robin)", "SCHED_SPORADIC" };

typedef struct {
    int    policy_id;
    double elapsed_sec;
    int    actual_policy;
} Result;

static volatile unsigned long sink = 0;

static void cpu_work(unsigned long n)
{
    unsigned long i, acc = 0;
    for (i = 0; i < n; ++i) {
        acc += i * i + (i ^ 0xCAFEBABEUL);
    }
    sink = acc;
}

int main(void)
{
    const char *shm_name = "/rtos06_sched_shm";
    int    shm_fd;
    Result *results;
    pid_t   pids[NUM_POLICIES];
    int     i, status;

    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (shm_fd == -1) { perror("shm_open"); return EXIT_FAILURE; }
    if (ftruncate(shm_fd, sizeof(Result) * NUM_POLICIES) == -1) {
        perror("ftruncate"); shm_unlink(shm_name); return EXIT_FAILURE;
    }
    results = (Result *)mmap(NULL, sizeof(Result) * NUM_POLICIES,
                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (results == MAP_FAILED) {
        perror("mmap"); shm_unlink(shm_name); return EXIT_FAILURE;
    }

    printf("=== RTOS-06: Scheduling Algorithm vs Execution Time ===\n");
    printf("All processes: priority = %d, workload = %lu iterations\n\n", FIXED_PRIORITY, WORKLOAD);

    /* --- fork one child per policy --------------------------------- */
    for (i = 0; i < NUM_POLICIES; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            shm_unlink(shm_name);
            return EXIT_FAILURE;
        }

        if (pids[i] == 0) {
            struct sched_param sp;
            struct timeval     t_start, t_end;
            int ret;

            int cfd = shm_open(shm_name, O_RDWR, 0600);
            Result *cres = (Result *)mmap(NULL, sizeof(Result) * NUM_POLICIES, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);

            memset(&sp, 0, sizeof(sp));
            sp.sched_priority = FIXED_PRIORITY;

            ret = sched_setscheduler(0, policies[i], &sp);
            if (ret == -1) {
                fprintf(stderr, "[child %d] sched_setscheduler(%s) failed: %s\n",
                        i, policy_names[i], strerror(errno));
                // still run and record whatever policy was inherited
            }

            cres[i].actual_policy = sched_getscheduler(0);
            cres[i].policy_id     = i;

            gettimeofday(&t_start, NULL);
            cpu_work(WORKLOAD);
            gettimeofday(&t_end, NULL);

            cres[i].elapsed_sec = (t_end.tv_sec  - t_start.tv_sec) +
                                  (t_end.tv_usec - t_start.tv_usec) * 1e-6;

            munmap(cres, sizeof(Result) * NUM_POLICIES);
            close(cfd);
            _exit(EXIT_SUCCESS);
        }
    }

    for (i = 0; i < NUM_POLICIES; ++i) {
        waitpid(pids[i], &status, 0);
    }

    for (i = 0; i < NUM_POLICIES; ++i) {
        printf("%s: %.6f seconds (code: %d)\n", policy_names[i], results[i].elapsed_sec, results[i].actual_policy);
    }

    /* Key observations:
     * SCHED_FIFO – runs until it voluntarily yields or is preempted; no timeslice limit at same priority.
     * SCHED_RR – same as FIFO but preempted after a timeslice; other ready processes of equal priority get a turn.
     * SCHED_SPORADIC – priority decays by 1 after each budget exhaustion; restored on the next blocking event.
     * With a single CPU-bound process per policy the times should be similar;
     * the difference becomes pronounced when multiple processes of the SAME priority compete for the CPU.
     */

    // clean
    munmap(results, sizeof(Result) * NUM_POLICIES);
    close(shm_fd);
    shm_unlink(shm_name);

    return EXIT_SUCCESS;
}