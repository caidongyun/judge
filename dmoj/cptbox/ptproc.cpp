#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ptbox.h"

pt_process *pt_alloc_process(pt_debugger *debugger) {
    return new pt_process(debugger);
}

void pt_free_process(pt_process *process) {
    delete process;
}

pt_process::pt_process(pt_debugger *debugger) :
    pid(0), callback(NULL), context(NULL), debugger(debugger),
    event_proc(NULL), event_context(NULL), _trace_syscalls(true),
    _initialized(false)
{
    memset(&exec_time, 0, sizeof exec_time);
    memset(handler, 0, sizeof handler);
    debugger->set_process(this);
}

void pt_process::set_callback(pt_handler_callback callback, void *context) {
    this->callback = callback;
    this->context = context;
}

void pt_process::set_event_proc(pt_event_callback callback, void *context) {
    this->event_proc = callback;
    this->event_context = context;
}

int pt_process::set_handler(int syscall, int handler) {
    if (syscall >= MAX_SYSCALL || syscall < 0)
        return 1;
    this->handler[syscall] = handler;
    return 0;
}

int pt_process::dispatch(int event, unsigned long param) {
    if (event_proc != NULL)
        return event_proc(event_context, event, param);
    return -1;
}

int pt_process::spawn(pt_fork_handler child, void *context) {
    pid_t pid = fork();
    if (pid == -1)
        return 1;
    if (pid == 0)
        _exit(child(context));
    this->pid = pid;
    debugger->new_process();
    return 0;
}

int pt_process::protection_fault(int syscall) {
    dispatch(PTBOX_EVENT_PROTECTION, syscall);
    dispatch(PTBOX_EVENT_EXITING, PTBOX_EXIT_PROTECTION);
    kill(pid, SIGKILL);
    return PTBOX_EXIT_PROTECTION;
}

int pt_process::monitor() {
    bool in_syscall = false, first = true, spawned = false;
    struct timespec start, end, delta;
    int status, exit_reason = PTBOX_EXIT_NORMAL;
    siginfo_t si;

    while (true) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        wait4(pid, &status, 0, &_rusage);
        clock_gettime(CLOCK_MONOTONIC, &end);
        timespec_sub(&end, &start, &delta);
        timespec_add(&exec_time, &delta, &exec_time);
        int signal = 0;

        if (WIFEXITED(status) || WIFSIGNALED(status))
            break;

        if (first) {
            dispatch(PTBOX_EVENT_ATTACH, 0);
            // This is right after SIGSTOP is received:
            ptrace(PTRACE_SETOPTIONS, pid, NULL, PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXIT);
        }

        if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == (0x80 | SIGTRAP)) {
                int syscall = debugger->syscall();
                in_syscall ^= true;
                //printf("%s syscall %d\n", in_syscall ? "Enter" : "Exit", syscall);

                if (!spawned) {
                    // This might seem odd, and you may ask yourself: "does execve not return if the process hits an
                    // rlimit and gets SIGKILLed?"
                    //
                    // No, it doesn't. See the session below.
                    //      $ ulimit -Sv50000
                    //      $ strace ./a.out
                    //      execve("./a.out", ["./a.out"], [/* 17 vars */] <unfinished ...>
                    //      +++ killed by SIGKILL +++
                    //      Killed
                    //
                    // From this we can see that execve doesn't return (<unfinished ...>) if the process fails to
                    // initialize, so we don't need to wait until the next non-execve syscall to set
                    // _initialized to true - if it exited execve, it's good to go.
                    if (!in_syscall && syscall == debugger->execve_syscall())
                        spawned = this->_initialized = true;
                } else if (in_syscall) {
                    if (syscall < MAX_SYSCALL) {
                        switch (handler[syscall]) {
                            case PTBOX_HANDLER_ALLOW:
                                break;
                            case PTBOX_HANDLER_STDOUTERR: {
                                int arg0 = debugger->arg0();
                                if (arg0 != 1 && arg0 != 2)
                                    exit_reason = protection_fault(syscall);
                                break;
                            }
                            case PTBOX_HANDLER_CALLBACK:
                                if (callback(context, syscall))
                                    break;
                                //printf("Killed by callback: %d\n", syscall);
                                exit_reason = protection_fault(syscall);
                                continue;
                            default:
                                // Default is to kill, safety first.
                                //printf("Killed by DISALLOW or None: %d\n", syscall);
                                exit_reason = protection_fault(syscall);
                                continue;
                        }
                    }
                } else if (debugger->on_return_callback) {
                    debugger->on_return_callback(debugger->on_return_context, syscall);
                    debugger->on_return_callback = NULL;
                    debugger->on_return_context = NULL;
                }
            } else {
                switch (WSTOPSIG(status)) {
                    case SIGTRAP:
                        switch (status >> 16) {
                            case PTRACE_EVENT_EXIT:
                                if (exit_reason != PTBOX_EXIT_NORMAL)
                                    dispatch(PTBOX_EVENT_EXITING, PTBOX_EXIT_NORMAL);
                        }
                        break;
                    default:
                        signal = WSTOPSIG(status);
                }
                if(!first) // *** Don't set _signal to SIGSTOP if this is the /first/ SIGSTOP
                    dispatch(PTBOX_EVENT_SIGNAL, WSTOPSIG(status));
            }
        }
        // Pass NULL as signal in case of our first SIGSTOP because the runtime tends to resend it, making all our
        // work for naught. Like abort(), it catches the signal, prints something (^Z?) and then resends it.
        // Doing this prevents a second SIGSTOP from being dispatched to our event handler above. ***
        ptrace(_trace_syscalls ? PTRACE_SYSCALL : PTRACE_CONT, pid, NULL, first ? NULL : (void*) signal);
        first = false;
    }
    dispatch(PTBOX_EVENT_EXITED, exit_reason);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
}
