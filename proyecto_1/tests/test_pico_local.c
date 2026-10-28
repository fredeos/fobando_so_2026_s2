#include <assert.h>
#include <stdio.h>
#include "local_test.h"

static Scheduler *observed_scheduler;
static unsigned observed_calls;
static void observe_selection(uint32_t id)
{
    assert(observed_scheduler->current_tick == observed_calls);

    if (id != UINT32_MAX) {
        int job = scheduler_select_job(observed_scheduler);
        assert(job >= 0);
        assert(observed_scheduler->jobs[job].ut_remaining > 0u);
        assert(observed_scheduler->jobs[job].task->id == id);
    }
    ++observed_calls;
}

static void compare_algorithms(void)
{
    Scheduler rms, edf;
    assert(local_tasks_init(&rms, 1u, RMS));
    assert(local_tasks_init(&edf, 1u, EDF));
    assert(rms.task_count == 5u && edf.task_count == 5u);
    unsigned seen = 0;
    unsigned differences = 0;
    observed_scheduler = &edf;
    edf.on_selection = observe_selection;
    for (unsigned tick=0; tick<3500u; ++tick) {
        uint32_t r, e;
        assert(scheduler_run_tick(&rms, &r));
        assert(scheduler_run_tick(&edf, &e));
        if (r != e) ++differences;
        if (e != UINT32_MAX) seen |= 1u << e;
    }
    assert(observed_calls == 3500u);
    assert((seen & 0x1eu) == 0x1eu);
    assert(differences > 0u);
    assert(rms.deadline_miss_count > 0u);
    assert(edf.deadline_miss_count == 0u);
    printf("3500 ticks: RMS misses=%u; EDF misses=%u\n",
           (unsigned)rms.deadline_miss_count, (unsigned)edf.deadline_miss_count);
}

static void check_button(void)
{
    LocalButton button = {0};
    assert(!local_button_update(&button, true, 1000, 2000));
    assert(!local_button_update(&button, false, 3000, 4000));
    assert(!local_button_update(&button, true, 5000, 34999));
    assert(local_button_update(&button, true, 5000, 35000));
    assert(!local_button_update(&button, true, 5000, 100000)); /* sostenido */
    assert(!local_button_update(&button, false, 110000, 120000));
    assert(!local_button_update(&button, true, 125000, 160000)); /* rebote al soltar */
    assert(!local_button_update(&button, false, 170000, 200000));
    assert(local_button_update(&button, true, 210000, 240000));
}

static void check_sporadic(void)
{
    Scheduler sc;
    assert(local_tasks_init(&sc, 1u, EDF));
    assert(scheduler_register_sporadic_event(&sc));
    assert(!scheduler_register_sporadic_event(&sc));
    uint32_t selected;
    for (unsigned i=0; i<19u; ++i) assert(scheduler_run_tick(&sc, &selected));
    assert(!scheduler_register_sporadic_event(&sc));
    assert(sc.sporadic_rejected_count == 2u);
    assert(scheduler_run_tick(&sc, &selected));
    assert(scheduler_register_sporadic_event(&sc));
    for (unsigned i=0; i<20u; ++i) assert(scheduler_run_tick(&sc, &selected));
    assert(sc.tasks[sc.sporadic_task_index].id == 5u);
    assert(sc.sporadic_completed_count == 2u);
    assert(sc.deadline_miss_count == 0u);
}

int main(void)
{
    compare_algorithms();
    check_button();
    check_sporadic();
    puts("OK: comparacion RMS/EDF, debounce y tarea esporadica");
    return 0;
}
