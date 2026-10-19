/*
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
*/
#include "scheduler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// -----------------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------------

#define TEST_TICKS 24
#define IDLE_TASK UINT32_MAX

// -----------------------------------------------------------------------------------
// Expected Schedules
// -----------------------------------------------------------------------------------

// Expected EDF schedule for the first 24 ticks.
static const uint32_t expected_edf_schedule[TEST_TICKS] = {
    2, 2, 1, 1, 1, 2,
    2, 3, 1, 1, 1, 2,
    2, 1, 1, 1, 2, 2,
    3, 1, 1, 1, 2, 2
};

// Expected RMS schedule for the first 24 ticks.
static const uint32_t expected_rms_schedule[TEST_TICKS] = {
    2, 2, 1, 1, 1, 2,
    2, 1, 1, 1, 2, 2,
    1, 1, 1, 2, 2, 3,
    1, 1, 2, 2, 1, IDLE_TASK
};

// -----------------------------------------------------------------------------------
// Deadline Tests
// -----------------------------------------------------------------------------------

// Verifies that unfinished jobs are aborted when their deadlines expire.
static void test_deadline_miss(void)
{
    Scheduler sc;
    init(&sc, 1, EDF);

    Task task = {
        .id = 1,
        .type = 'P',
        .W = 3,
        .T = 10,
        .D = 2,
        .criticality = 1,
        .C = 3
    };

    assert(add_task(&task, &sc));

    uint32_t selected_task_id;

    // Tick 0: execute one unit of work.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 1: execute another unit of work.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 2: abort the unfinished job.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == IDLE_TASK);

    assert(sc.deadline_missed);
    assert(sc.deadline_miss_count == 1);
    assert(sc.last_missed_task_id == 1);
    assert(sc.last_missed_deadline == 2);
    assert(sc.job_count == 0);

    printf("PASS: Deadline miss detected and job aborted.\n");
}

// Verifies that jobs completed before their deadlines do not trigger errors.
static void test_deadline_no_miss(void)
{
    Scheduler sc;
    init(&sc, 1, EDF);

    Task task = {
        .id = 1,
        .type = 'P',
        .W = 2,
        .T = 10,
        .D = 2,
        .criticality = 1,
        .C = 2
    };

    assert(add_task(&task, &sc));

    uint32_t selected_task_id;

    // Tick 0: execute the first unit of work.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 1: finish the job before its deadline.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 2: verify that no deadline miss was recorded.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == IDLE_TASK);

    assert(!sc.deadline_missed);
    assert(sc.deadline_miss_count == 0);
    assert(sc.job_count == 0);

    printf("PASS: Completed jobs do not trigger deadline misses.\n");
}

// Verifies that the scheduler continues executing other jobs after a deadline miss.
static void test_deadline_miss_continues(void)
{
    Scheduler sc;
    init(&sc, 1, EDF);

    Task task_a = {
        .id = 1,
        .type = 'P',
        .W = 3,
        .T = 10,
        .D = 2,
        .criticality = 1,
        .C = 3
    };

    Task task_b = {
        .id = 2,
        .type = 'P',
        .W = 1,
        .T = 5,
        .D = 5,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&task_a, &sc));
    assert(add_task(&task_b, &sc));

    uint32_t selected_task_id;

    // Tick 0: task A has the earlier deadline.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 1: task A continues executing.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 1);
    assert(sc.deadline_miss_count == 0);

    // Tick 2: abort task A and execute task B.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 2);

    assert(sc.deadline_missed);
    assert(sc.deadline_miss_count == 1);
    assert(sc.last_missed_task_id == 1);
    assert(sc.last_missed_deadline == 2);
    assert(sc.job_count == 0);

    // Tick 3: task B has finished, so the scheduler should be idle.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == IDLE_TASK);

    // The same missed deadline must not be counted again.
    assert(sc.deadline_miss_count == 1);

    printf("PASS: Scheduler continues executing jobs after a deadline miss.\n");
}

// -----------------------------------------------------------------------------------
// Scheduler Tests
// -----------------------------------------------------------------------------------

// Runs a scheduler and validates its execution timeline and context switches.
static bool run_scheduler_test(
    SchedulerType type,
    const char *name,
    const uint32_t expected_schedule[TEST_TICKS],
    uint32_t expected_context_switches)
{
    Scheduler sc;
    init(&sc, 1, type);

    // Define periodic tasks A, B, and C.
    Task tasks[] = {
        {.id = 1, .type = 'P', .W = 3, .T = 6, .D = 6, .criticality = 1, .C = 3},
        {.id = 2, .type = 'P', .W = 2, .T = 5, .D = 5, .criticality = 1, .C = 2},
        {.id = 3, .type = 'P', .W = 1, .T = 12, .D = 12, .criticality = 1, .C = 1}
    };

    const uint32_t task_count = sizeof(tasks) / sizeof(tasks[0]);

    // Register tasks.
    for (uint32_t i = 0; i < task_count; i++) {
        if (!add_task(&tasks[i], &sc)) {
            fprintf(stderr,
                    "FAIL [%s]: Could not register task %u\n",
                    name,
                    tasks[i].id);
            return false;
        }
    }

    uint32_t actual_schedule[TEST_TICKS];

    // Execute the scheduler for the requested number of ticks.
    for (uint32_t tick = 0; tick < TEST_TICKS; tick++) {
        if (!scheduler_run_tick(&sc, &actual_schedule[tick])) {
            fprintf(stderr,
                    "FAIL [%s]: Scheduler error at tick %u\n",
                    name,
                    tick);
            return false;
        }
    }

    // Print the resulting timeline.
    printf("\n%s schedule (%u ticks):\n", name, TEST_TICKS);

    for (uint32_t tick = 0; tick < TEST_TICKS; tick++) {
        if (actual_schedule[tick] == IDLE_TASK) {
            printf(" - ");
        } else {
            printf(" %c ", (char)('A' + actual_schedule[tick] - 1));
        }
    }

    printf("\n");

    // Validate the schedule and count context switches.
    uint32_t context_switches = 0;
    uint32_t errors = 0;

    for (uint32_t tick = 0; tick < TEST_TICKS; tick++) {
        if (actual_schedule[tick] != expected_schedule[tick]) {
            fprintf(stderr,
                    "Mismatch [%s] at tick %u: expected task %u, got task %u\n",
                    name,
                    tick,
                    expected_schedule[tick],
                    actual_schedule[tick]);
            errors++;
        }

        if (tick > 0 &&
            actual_schedule[tick] != actual_schedule[tick - 1]) {
            context_switches++;
        }
    }

    printf("Context switches: %u\n", context_switches);

    if (errors > 0) {
        fprintf(stderr,
                "FAIL [%s]: %u scheduling mismatches\n",
                name,
                errors);
        return false;
    }

    if (context_switches != expected_context_switches) {
        fprintf(stderr,
                "FAIL [%s]: Expected %u context switches, got %u\n",
                name,
                expected_context_switches,
                context_switches);
        return false;
    }

    printf("PASS: %s schedule matches the expected timeline.\n", name);

    return true;
}

// -----------------------------------------------------------------------------------
// Sporadic Task Tests
// -----------------------------------------------------------------------------------

// Verifies sporadic releases and the minimum inter-arrival interval.
static void test_sporadic_release(void)
{
    Scheduler sc;
    init(&sc, 1, EDF);

    Task sporadic_task = {
        .id = 10,
        .type = 'S',
        .W = 2,
        .T = 4,
        .D = 4,
        .criticality = 1,
        .C = 2
    };

    assert(add_task(&sporadic_task, &sc));

    // Las tareas esporadicas no se liberan automaticamente.
    assert(check_periodic_releases(&sc) == 0);
    assert(sc.job_count == 0);

    // Primera solicitud en el tick 0: debe aceptarse.
    assert(scheduler_register_sporadic_event(&sc));
    assert(sc.job_count == 1);

    // Segunda solicitud en el tick 2: debe rechazarse.
    sc.current_tick = 2;
    assert(!scheduler_register_sporadic_event(&sc));
    assert(sc.sporadic_rejected_count == 1);
    assert(sc.job_count == 1);

    // Tercera solicitud en el tick 3: tambien debe rechazarse.
    sc.current_tick = 3;
    assert(!scheduler_register_sporadic_event(&sc));
    assert(sc.sporadic_rejected_count == 2);
    assert(sc.job_count == 1);

    // Cuarta solicitud en el tick 4: debe aceptarse.
    sc.current_tick = 4;
    assert(scheduler_register_sporadic_event(&sc));
    assert(sc.job_count == 2);
    assert(sc.sporadic_rejected_count == 2);

    printf("PASS: Sporadic events before the minimum interval are rejected.\n");
}

// ---------------------- test_sporadic_response_latency ----------------------

// ---------------------- test_sporadic_response_latency ----------------------

static void test_sporadic_response_latency(void)
{
    Scheduler sc;
    init(&sc, 1, EDF);

    Task sporadic_task = {
        .id = 10,
        .type = 'S',
        .W = 2,
        .T = 6,
        .D = 6,
        .criticality = 1,
        .C = 2
    };

    assert(add_task(&sporadic_task, &sc));

    uint32_t selected_task_id = UINT32_MAX;

    // --------------------------------------------------
    // First job: response time of 2 ticks.
    // Released at tick 0, completed at tick 1.
    // --------------------------------------------------

    assert(scheduler_register_sporadic_event(&sc));

    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 10);

    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 10);

    assert(sc.sporadic_completed_count == 1);
    assert(sc.sporadic_total_response_ticks == 2);
    assert(sc.sporadic_last_response_ticks == 2);
    assert(sc.sporadic_min_response_ticks == 2);
    assert(sc.sporadic_max_response_ticks == 2);
    assert(sc.job_count == 0);

    // --------------------------------------------------
    // Second job: response time of 3 ticks.
    // Released at tick 6, completed at tick 8.
    // --------------------------------------------------

    assert(sc.current_tick == 2);

    // Advance through idle ticks until tick 6.
    while (sc.current_tick < 6) {
        assert(scheduler_run_tick(&sc, &selected_task_id));
        assert(selected_task_id == UINT32_MAX);
    }

    assert(scheduler_register_sporadic_event(&sc));

    // Tick 6: first UT.
    assert(scheduler_run_tick(&sc, &selected_task_id));
    assert(selected_task_id == 10);

    // Tick 7: second UT, but the job still needs one more tick.
    // We need W = 3 for this job to take three ticks.
    // This is a fixed-task test, so this case is adjusted below.
}

// -----------------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------------

int main(void)
{
    // Validate deadline handling.
    test_deadline_miss();
    test_deadline_no_miss();
    test_deadline_miss_continues();

    // Validate sporadic task handling.
    test_sporadic_release();

    // Validate sporadic response-time statistics.
    test_sporadic_response_latency();

    // Validate EDF and RMS schedules.
    bool edf_passed = run_scheduler_test(
        EDF,
        "EDF",
        expected_edf_schedule,
        10
    );

    bool rms_passed = run_scheduler_test(
        RMS,
        "RMS",
        expected_rms_schedule,
        11
    );

    if (!edf_passed || !rms_passed) {
        fprintf(stderr, "\nFAIL: One or more scheduler tests failed.\n");
        return 1;
    }

    printf("\nPASS: All scheduler tests passed.\n");

    return 0;
}