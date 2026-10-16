#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "scheduler.h"
#include "task.h"

// ---------------------------------------------------------
// TEST 1: Scheduler initialization
// ---------------------------------------------------------

void test_init(void) {

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

    printf("[SUCCESS] Scheduler initialization\n");
}


// ---------------------------------------------------------
// TEST 2: Add tasks
// ---------------------------------------------------------

void test_add_task(void) {

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

    assert(add_task(&task1, &sc) == true);
    assert(add_task(&task2, &sc) == true);

    assert(sc.task_count == 2);

    assert(sc.tasks[0].id == 1);
    assert(sc.tasks[0].W == 250);
    assert(sc.tasks[0].T == 10);

    assert(sc.tasks[1].id == 2);
    assert(sc.tasks[1].W == 400);
    assert(sc.tasks[1].T == 20);

    // Invalid task: T = 0
    Task invalid_task = {
        .id = 3,
        .type = 'P',
        .W = 100,
        .T = 0,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&invalid_task, &sc) == false);
    assert(sc.task_count == 2);

    // Null pointers
    assert(add_task(NULL, &sc) == false);
    assert(add_task(&task1, NULL) == false);

    printf("[SUCCESS] Task registration\n");
}


// ---------------------------------------------------------
// TEST 3: Maximum task capacity
// ---------------------------------------------------------

void test_max_tasks(void) {

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

        assert(add_task(&task, &sc) == true);
    }

    assert(sc.task_count == MAX_TASKS);

    Task extra = {
        .id = 100,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&extra, &sc) == false);
    assert(sc.task_count == MAX_TASKS);

    printf("[SUCCESS] Maximum task capacity\n");
}


// ---------------------------------------------------------
// TEST 4: Free index management
// ---------------------------------------------------------

void test_free_indices(void) {

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

    printf("[SUCCESS] Free index allocation\n");
}


// ---------------------------------------------------------
// TEST 5: Release a Job
// ---------------------------------------------------------

void test_release_job(void) {

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

    assert(add_task(&task, &sc) == true);

    sc.current_tick = 5;

    assert(release_job(&sc, 0) == true);

    assert(sc.job_count == 1);
    assert(sc.free_count == MAX_JOBS - 1);

    // First allocated index
    uint32_t index = MAX_JOBS - 1;

    Job *job = &sc.jobs[index];

    assert(job->active == true);

    // Job must point to the Task stored in Scheduler
    assert(job->task == &sc.tasks[0]);

    assert(job->task->id == 1);

    assert(job->release == 5);
    assert(job->deadline == 15);

    assert(job->ut_remaining == 250);

    printf("[SUCCESS] Job release\n");
}


// ---------------------------------------------------------
// TEST 6: Free a Job and reuse its position
// ---------------------------------------------------------

void test_free_job(void) {

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

    assert(add_task(&task, &sc) == true);

    assert(release_job(&sc, 0) == true);

    uint32_t index = MAX_JOBS - 1;

    assert(sc.jobs[index].active == true);
    assert(sc.job_count == 1);

    // Free the Job
    assert(free_job(&sc, index) == true);

    assert(sc.jobs[index].active == false);
    assert(sc.job_count == 0);
    assert(sc.free_count == MAX_JOBS);

    // Cannot free the same Job twice
    assert(free_job(&sc, index) == false);

    // Reuse the released position
    assert(release_job(&sc, 0) == true);

    assert(sc.jobs[index].active == true);
    assert(sc.job_count == 1);
    assert(sc.free_count == MAX_JOBS - 1);

    printf("[SUCCESS] Job deallocation and reuse\n");
}


// ---------------------------------------------------------
// TEST 7: Periodic releases
// ---------------------------------------------------------

void test_periodic_releases(void) {

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

    assert(add_task(&taskA, &sc) == true);
    assert(add_task(&taskB, &sc) == true);
    assert(add_task(&taskS, &sc) == true);

    printf("\nPeriodic release simulation:\n");

    for (uint32_t tick = 0; tick <= 20; tick++) {

        sc.current_tick = tick;

        uint32_t released = check_periodic_releases(&sc);

        printf("Tick %2u: %u Jobs released\n",
               (unsigned)tick,
               (unsigned)released);

        uint32_t expected = 0;

        if (tick % 5 == 0) {
            expected++;
        }

        if (tick % 10 == 0) {
            expected++;
        }

        assert(released == expected);
    }

    // A: ticks 0, 5, 10, 15, 20 = 5 Jobs
    // B: ticks 0, 10, 20 = 3 Jobs
    // S: no automatic releases

    assert(sc.job_count == 8);
    assert(sc.free_count == MAX_JOBS - 8);

    printf("[SUCCESS] Periodic releases\n");
}


// ---------------------------------------------------------
// TEST 8: Job capacity exhaustion
// ---------------------------------------------------------

void test_max_jobs(void) {

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

    assert(add_task(&task, &sc) == true);

    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        assert(release_job(&sc, 0) == true);
    }

    assert(sc.job_count == MAX_JOBS);
    assert(sc.free_count == 0);

    // No more positions available
    assert(release_job(&sc, 0) == false);

    assert(sc.job_count == MAX_JOBS);
    assert(sc.free_count == 0);

    printf("[SUCCESS] Maximum Job capacity\n");
}


// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

int main(void) {

    printf("====================================\n");
    printf("       SCHEDULER UNIT TESTS\n");
    printf("====================================\n\n");

    test_init();
    test_add_task();
    test_max_tasks();
    test_free_indices();
    test_release_job();
    test_free_job();
    test_periodic_releases();
    test_max_jobs();

    printf("\n====================================\n");
    printf("       ALL TESTS PASSED\n");
    printf("====================================\n");

    return 0;
}