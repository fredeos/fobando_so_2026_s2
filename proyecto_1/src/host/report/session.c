#include "host/report_session.h"
#include "host/report.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Los handlers solo modifican sig_atomic_t; el trabajo se realiza fuera de ellos */
static volatile sig_atomic_t interrupted;

static void request_interrupt(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void short_pause(void)
{
    struct timespec pause = {.tv_nsec = 10000000L};
    nanosleep(&pause, NULL);
}

static bool successful(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Retirar hijos terminados sin bloquear la lectura de la tecla q */
static size_t reap_workers(pid_t *pids, size_t count, bool *failed)
{
    size_t alive = 0u;
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == 0) continue;
        int status;
        pid_t result = waitpid(pids[i], &status, WNOHANG);
        if (result == pids[i]) {
            if (!successful(status)) *failed = true;
            pids[i] = 0;
        } else if (result < 0 && errno != EINTR) {
            *failed = true;
            pids[i] = 0;
        } else ++alive;
    }
    return alive;
}

/*
 * El cierre normal es cooperativo. Un driver que no responde se termina con
 * un límite acotado y se declara el reporte incompleto, sin fingir un STOP.
 */
static void finish_workers(EventQueue *queue, pid_t *pids, size_t count, bool *failed)
{
    event_queue_request_stop(queue);
    uint64_t start = monotonic_ms();
    bool terminated = false, killed = false;
    while (reap_workers(pids, count, failed) > 0u) {
        uint64_t elapsed = monotonic_ms() - start;
        if (elapsed >= 3000u && !terminated) {
            *failed = true;
            event_queue_mark_incomplete(queue);
            for (size_t i = 0; i < count; ++i) if (pids[i] > 0) kill(pids[i], SIGTERM);
            terminated = true;
        }
        if (elapsed >= 3500u && !killed) {
            for (size_t i = 0; i < count; ++i) if (pids[i] > 0) kill(pids[i], SIGKILL);
            killed = true;
        }
        short_pause();
    }
}

int report_session_run(const char *directory, const ReportWorker *workers, size_t count)
{
    if (directory == NULL || directory[0] == '\0' || workers == NULL || count == 0u ||
        count > SIZE_MAX / sizeof(pid_t)) return 1;
    for (size_t i = 0; i < count; ++i)
        if (workers[i].run == NULL || workers[i].stop == NULL) return 1;

    struct stat directory_info;
    if ((mkdir(directory, 0775) != 0 && errno != EEXIST) ||
        stat(directory, &directory_info) != 0 || !S_ISDIR(directory_info.st_mode))
        return 1;

    EventQueue *queue = event_queue_create();
    pid_t *pids = calloc(count, sizeof(*pids));
    if (queue == NULL || pids == NULL) {
        event_queue_destroy(queue); free(pids); return 1;
    }

    struct termios original;
    bool terminal_changed = false, failed = false;
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &original) != 0) goto setup_fail;
        struct termios immediate = original;
        immediate.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        immediate.c_cc[VMIN] = 1;
        immediate.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &immediate) != 0) goto setup_fail;
        terminal_changed = true;
    }

    struct sigaction action = {0}, old_int, old_term;
    action.sa_handler = request_interrupt;
    sigemptyset(&action.sa_mask);
    interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int) != 0) goto setup_fail;
    if (sigaction(SIGTERM, &action, &old_term) != 0) {
        sigaction(SIGINT, &old_int, NULL);
        goto setup_fail;
    }
    fflush(NULL);
    pid_t reporter = fork();
    if (reporter == 0) {
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        _exit(report_run(queue, directory));
    }
    if (reporter < 0) failed = true;

    for (size_t i = 0; i < count && !failed; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) {
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, SIG_DFL);
            int result = workers[i].run(queue, workers[i].context);
            int stopped = workers[i].stop(queue, workers[i].context);
            if (result != 0 || stopped != 0) event_queue_mark_incomplete(queue);
            _exit(result == 0 && stopped == 0 ? 0 : 1);
        }
        if (pids[i] < 0) { pids[i] = 0; failed = true; }
    }

    bool reporter_reaped = false;
    while (!failed && !interrupted && !event_queue_stop_requested(queue)) {
        if (reap_workers(pids, count, &failed) == 0u || failed) break;
        int status;
        if (waitpid(reporter, &status, WNOHANG) == reporter) {
            reporter_reaped = true;
            failed = true;
            break;
        }
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        int ready = poll(&input, 1u, 50);
        if (ready < 0) {
            if (errno != EINTR) failed = true;
        } else if (ready > 0) {
            if (input.revents & (POLLERR | POLLNVAL)) { failed = true; break; }
            if (input.revents & (POLLIN | POLLHUP)) {
                char keys[64];
                ssize_t length = read(STDIN_FILENO, keys, sizeof(keys));
                if (length == 0) break;
                if (length < 0 && errno != EINTR) { failed = true; break; }
                for (ssize_t i = 0; i < length; ++i)
                    if (keys[i] == 'q') interrupted = 1;
            }
        }
    }

    finish_workers(queue, pids, count, &failed);
    if (failed) event_queue_mark_incomplete(queue);
    if (event_queue_close(queue) != EVENT_QUEUE_OK) failed = true;

    if (reporter > 0 && !reporter_reaped) {
        uint64_t start = monotonic_ms();
        for (;;) {
            int status;
            pid_t result = waitpid(reporter, &status, WNOHANG);
            if (result == reporter) {
                if (!successful(status)) failed = true;
                break;
            }
            if (result < 0 && errno != EINTR) { failed = true; break; }
            if (monotonic_ms() - start > 30000u) {
                failed = true;
                kill(reporter, SIGKILL);
                while (waitpid(reporter, &status, 0) < 0 && errno == EINTR) {}
                break;
            }
            short_pause();
        }
    }

    if (terminal_changed && tcsetattr(STDIN_FILENO, TCSANOW, &original) != 0) failed = true;
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    if (failed) event_queue_mark_incomplete(queue);
    event_queue_destroy(queue);
    free(pids);
    return failed ? 1 : 0;

setup_fail:
    if (terminal_changed) tcsetattr(STDIN_FILENO, TCSANOW, &original);
    event_queue_destroy(queue);
    free(pids);
    return 1;
}
