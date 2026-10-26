#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "scheduler.h"
#include "edf.h"

// ---------------------------------------------------------
// TEST 1: Select Job with earliest deadline
// ---------------------------------------------------------

void test_edf_earliest_deadline(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    SchedulerTask taskA = {
        .id = 1,
        .type = 'P',
        .W = 200,
        .T = 20,
        .D = 20,
        .criticality = 1,
        .C = 2
    };

    SchedulerTask taskB = {
        .id = 2,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 2,
        .C = 1
    };

    SchedulerTask taskC = {
        .id = 3,
        .type = 'P',
        .W = 300,
        .T = 5,
        .D = 5,
        .criticality = 3,
        .C = 3
    };

    assert(add_task(&taskA, &sc));
    assert(add_task(&taskB, &sc));
    assert(add_task(&taskC, &sc));

    assert(release_job(&sc, 0));
    assert(release_job(&sc, 1));
    assert(release_job(&sc, 2));

    int selected = edf_select_job(&sc);

    assert(selected >= 0);
    assert(sc.jobs[selected].task->id == 3);

    printf("[SUCCESS] EDF selects earliest deadline\n");
}


// ---------------------------------------------------------
// TEST 2: Ignore inactive Jobs
// ---------------------------------------------------------

void test_edf_inactive_jobs(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    SchedulerTask taskA = {
        .id = 1,
        .type = 'P',
        .W = 100,
        .T = 5,
        .D = 5,
        .criticality = 1,
        .C = 1
    };

    SchedulerTask taskB = {
        .id = 2,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 2,
        .C = 1
    };

    assert(add_task(&taskA, &sc));
    assert(add_task(&taskB, &sc));

    assert(release_job(&sc, 0));
    assert(release_job(&sc, 1));

    // The first released Job occupies the last position.
    uint32_t indexA = MAX_JOBS - 1;

    assert(free_job(&sc, indexA));

    int selected = edf_select_job(&sc);

    assert(selected >= 0);
    assert(sc.jobs[selected].task->id == 2);

    printf("[SUCCESS] EDF ignores inactive Jobs\n");
}


// ---------------------------------------------------------
// TEST 3: Tie by release time
// ---------------------------------------------------------

void test_edf_release_tie(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    SchedulerTask task = {
        .id = 1,
        .type = 'P',
        .W = 200,
        .T = 10,
        .D = 15,
        .criticality = 1,
        .C = 2
    };

    assert(add_task(&task, &sc));

    // First Job: released at tick 0.
    sc.current_tick = 0;
    assert(release_job(&sc, 0));

    // Second Job: released at tick 10.
    sc.current_tick = 10;
    assert(release_job(&sc, 0));

    // Give both Jobs the same absolute deadline.
    sc.jobs[MAX_JOBS - 1].deadline = 25;
    sc.jobs[MAX_JOBS - 2].deadline = 25;

    int selected = edf_select_job(&sc);

    assert(selected >= 0);
    assert(sc.jobs[selected].release == 0);
    assert(sc.jobs[selected].task->id == 1);

    printf("[SUCCESS] EDF tie by oldest release\n");
}


// ---------------------------------------------------------
// TEST 4: Tie by SchedulerTask ID
// ---------------------------------------------------------

void test_edf_id_tie(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    SchedulerTask taskA = {
        .id = 10,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    SchedulerTask taskB = {
        .id = 5,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 10,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&taskA, &sc));
    assert(add_task(&taskB, &sc));

    sc.current_tick = 0;

    assert(release_job(&sc, 0));
    assert(release_job(&sc, 1));

    // Give both Jobs the same absolute deadline.
    sc.jobs[MAX_JOBS - 1].deadline = 10;
    sc.jobs[MAX_JOBS - 2].deadline = 10;

    int selected = edf_select_job(&sc);

    assert(selected >= 0);
    assert(sc.jobs[selected].task->id == 5);

    printf("[SUCCESS] EDF tie by lowest SchedulerTask ID\n");
}


// ---------------------------------------------------------
// TEST 5: Complete tie by array index
// ---------------------------------------------------------

void test_edf_index_tie(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    SchedulerTask task = {
        .id = 1,
        .type = 'P',
        .W = 100,
        .T = 10,
        .D = 15,
        .criticality = 1,
        .C = 1
    };

    assert(add_task(&task, &sc));

    sc.current_tick = 0;

    assert(release_job(&sc, 0));
    assert(release_job(&sc, 0));

    // Force identical deadlines and release times.
    sc.jobs[MAX_JOBS - 1].deadline = 15;
    sc.jobs[MAX_JOBS - 2].deadline = 15;

    sc.jobs[MAX_JOBS - 1].release = 0;
    sc.jobs[MAX_JOBS - 2].release = 0;

    // Both Jobs have identical priority attributes.
    // The lower array index must win.
    int selected = edf_select_job(&sc);

    assert(selected == MAX_JOBS - 2);

    printf("[SUCCESS] EDF complete tie by array index\n");
}


// ---------------------------------------------------------
// TEST 6: No active Jobs
// ---------------------------------------------------------

void test_edf_no_jobs(void)
{
    Scheduler sc;
    init(&sc, 100, EDF);

    int selected = edf_select_job(&sc);

    assert(selected == -1);

    // Also verify NULL pointer.
    assert(edf_select_job(NULL) == -1);

    printf("[SUCCESS] EDF no active Jobs\n");
}


// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

int main(void)
{
    printf("====================================\n");
    printf("          EDF UNIT TESTS\n");
    printf("====================================\n\n");

    test_edf_earliest_deadline();
    test_edf_inactive_jobs();
    test_edf_release_tie();
    test_edf_id_tie();
    test_edf_index_tie();
    test_edf_no_jobs();

    printf("\n====================================\n");
    printf("        ALL EDF TESTS PASSED\n");
    printf("====================================\n");

    return 0;
}