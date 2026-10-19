#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "scheduler.h"
#include "task.h"

// ---------------------------------------------------------
// TEST 1: Scheduler initialization
// ---------------------------------------------------------

void test_init(void)
{
    Scheduler sc;

    init(&sc, 100, RMS);

    assert(sc.type == RMS);
    assert(sc.task_count == 0);
    assert(sc.job_count == 0);
    assert(sc.current_tick == 0);
    assert(sc.sn == 100);
    assert(sc.free_count == MAX_JOBS);

    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        assert(sc.free_index[i] == i);
        assert(sc.jobs[i].active == false);
    }

    // Verify EDF initialization as well.
    init(&sc, 50, EDF);

    assert(sc.type == EDF);
    assert(sc.task_count == 0);
    assert(sc.job_count == 0);
    assert(sc.current_tick == 0);
    assert(sc.sn == 50);
    assert(sc.free_count == MAX_JOBS);

    printf("[SUCCESS] Scheduler initialization\n");
}


// ---------------------------------------------------------
// TEST 2: Add tasks
// ---------------------------------------------------------

void test_add_task(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    Task task1 = {
        .id = 1,
        .type = 'P',
        .W = 250,
        .T = 10,
        .D = 10,
        .criticality = 2,
        .C = 3
    };

    Task task2 = {
        .id = 2,
        .type = 'P',
        .W = 400,
        .T = 20,
        .D = 20,
        .criticality = 1,
        .C = 4
    };

    assert(add_task(&task1, &sc));
    assert(add_task(&task2, &sc));

    assert(sc.task_count == 2);

    assert(sc.tasks[0].id == 1);
    assert(sc.tasks[0].W == 250);
    assert(sc.tasks[0].T == 10);

    assert(sc.tasks[1].id == 2);
    assert(sc.tasks[1].W == 400);
    assert(sc.tasks[1].T == 20);

    // Reject tasks with invalid periods.
    Task invalid_task = {
        .id = 3,
        .type = 'P',
        .W = 100,
        .T = 0,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(!add_task(&invalid_task, &sc));
    assert(sc.task_count == 2);

    // Reject null pointers.
    assert(!add_task(NULL, &sc));
    assert(!add_task(&task1, NULL));

    printf("[SUCCESS] Task registration\n");
}


// ---------------------------------------------------------
// TEST 3: Maximum task capacity
// ---------------------------------------------------------

void test_max_tasks(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        Task task = {
            .id = i + 1,
            .type = 'P',
            .W = 100,
            .T = 10,
            .D = 10,
            .criticality = 1,
            .C = 1
        };

        assert(add_task(&task, &sc));
    }

    assert(sc.task_count == MAX_TASKS);

    Task extra = {
        .id = MAX_TASKS + 1,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(!add_task(&extra, &sc));
    assert(sc.task_count == MAX_TASKS);

    printf("[SUCCESS] Maximum task capacity\n");
}


// ---------------------------------------------------------
// TEST 4: Free index management
// ---------------------------------------------------------

void test_free_indices(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    int index1 = get_free_index(&sc);
    int index2 = get_free_index(&sc);
    int index3 = get_free_index(&sc);

    assert(index1 == MAX_JOBS - 1);
    assert(index2 == MAX_JOBS - 2);
    assert(index3 == MAX_JOBS - 3);

    assert(index1 != index2);
    assert(index2 != index3);
    assert(index1 != index3);

    assert(sc.free_count == MAX_JOBS - 3);

    // Exhaust the free-index pool.
    for (uint32_t i = 3; i < MAX_JOBS; i++) {
        assert(get_free_index(&sc) >= 0);
    }

    assert(sc.free_count == 0);
    assert(get_free_index(&sc) == -1);

    printf("[SUCCESS] Free index allocation\n");
}


// ---------------------------------------------------------
// TEST 5: Release a Job
// ---------------------------------------------------------

void test_release_job(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    Task task = {
        .id = 1,
        .type = 'P',
        .W = 250,
        .T = 10,
        .D = 10,
        .criticality = 2,
        .C = 3
    };

    assert(add_task(&task, &sc));

    sc.current_tick = 5;

    assert(release_job(&sc, 0));

    assert(sc.job_count == 1);
    assert(sc.free_count == MAX_JOBS - 1);

    uint32_t index = MAX_JOBS - 1;
    Job *job = &sc.jobs[index];

    assert(job->active);
    assert(job->task == &sc.tasks[0]);
    assert(job->task->id == 1);

    assert(job->release == 5);
    assert(job->deadline == 15);
    assert(job->ut_remaining == 250);

    // Reject invalid task indices.
    assert(!release_job(&sc, sc.task_count));

    // Reject null scheduler pointers.
    assert(!release_job(NULL, 0));

    printf("[SUCCESS] Job release\n");
}


// ---------------------------------------------------------
// TEST 6: Free a Job and reuse its position
// ---------------------------------------------------------

void test_free_job(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    Task task = {
        .id = 1,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&task, &sc));
    assert(release_job(&sc, 0));

    uint32_t index = MAX_JOBS - 1;

    assert(sc.jobs[index].active);
    assert(sc.job_count == 1);

    // Free the Job.
    assert(free_job(&sc, index));

    assert(!sc.jobs[index].active);
    assert(sc.job_count == 0);
    assert(sc.free_count == MAX_JOBS);

    // Reject invalid and repeated deallocation.
    assert(!free_job(&sc, index));
    assert(!free_job(&sc, MAX_JOBS));

    // Reuse the released position.
    assert(release_job(&sc, 0));

    assert(sc.jobs[index].active);
    assert(sc.job_count == 1);
    assert(sc.free_count == MAX_JOBS - 1);

    printf("[SUCCESS] Job deallocation and reuse\n");
}


// ---------------------------------------------------------
// TEST 7: Periodic releases
// ---------------------------------------------------------

void test_periodic_releases(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    Task taskA = {
        .id = 1,
        .type = 'P',
        .W = 200,
        .T = 5,
        .D = 5,
        .criticality = 2,
        .C = 2
    };

    Task taskB = {
        .id = 2,
        .type = 'P',
        .W = 300,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 3
    };

    Task taskS = {
        .id = 3,
        .type = 'S',
        .W = 100,
        .T = 15,
        .D = 15,
        .criticality = 3,
        .C = 1
    };

    assert(add_task(&taskA, &sc));
    assert(add_task(&taskB, &sc));
    assert(add_task(&taskS, &sc));

    printf("\nPeriodic release simulation:\n");

    for (uint32_t tick = 0; tick <= 20; tick++) {
        sc.current_tick = tick;

        uint32_t released = check_periodic_releases(&sc);
        uint32_t expected = 0;

        if (tick % taskA.T == 0) {
            expected++;
        }

        if (tick % taskB.T == 0) {
            expected++;
        }

        // Sporadic tasks must not be released automatically.
        assert(released == expected);

        printf("Tick %2u: %u Jobs released\n",
               (unsigned)tick,
               (unsigned)released);
    }

    // Task A: ticks 0, 5, 10, 15, 20.
    // Task B: ticks 0, 10, 20.
    // Task S: no automatic releases.
    assert(sc.job_count == 8);
    assert(sc.free_count == MAX_JOBS - 8);

    printf("[SUCCESS] Periodic releases\n");
}


// ---------------------------------------------------------
// TEST 8: Job capacity exhaustion
// ---------------------------------------------------------

void test_max_jobs(void)
{
    Scheduler sc;
    init(&sc, 100, RMS);

    Task task = {
        .id = 1,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&task, &sc));

    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        assert(release_job(&sc, 0));
    }

    assert(sc.job_count == MAX_JOBS);
    assert(sc.free_count == 0);

    // No free positions remain.
    assert(!release_job(&sc, 0));

    assert(sc.job_count == MAX_JOBS);
    assert(sc.free_count == 0);

    printf("[SUCCESS] Maximum Job capacity\n");
}


// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

int main(void)
{
    printf("====================================\n");
    printf("       SCHEDULER UNIT TESTS\n");
    printf("====================================\n");

    SchedulerType types[] = {RMS, EDF};
    const char *names[] = {"RMS", "EDF"};

    for (uint32_t i = 0; i < 2; i++) {
        printf("\n====================================\n");
        printf("       TESTING %s\n", names[i]);
        printf("====================================\n\n");

        test_init();
        test_add_task();
        test_max_tasks();
        test_free_indices();
        test_release_job();
        test_free_job();
        test_periodic_releases();
        test_max_jobs();
    }

    printf("\n====================================\n");
    printf("       ALL TESTS PASSED\n");
    printf("====================================\n");

    return 0;
}