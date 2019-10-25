//
//  main.c
//  mlqfs
//
//  Created by Pierre Gabory on 24/10/2019.
//  Copyright © 2019 piergabory. All rights reserved.
//

#include "mlqfs.h"

#define MAX_PRIORITY 0
#define MIN_PRIORITY 2

static const int QUANTUM[3] = { 10, 30, 100 };
static const int DEMOTION[3] = { 1, 2, 0 };
static const int PROMOTION[3] = { 0, 2, 1 };

static Queue run;
static Queue io;
static Queue pending;

static unsigned int mlqfs_clock = 0;

int process_compare(void* lhs, void* rhs) {
    Process *left = lhs;
    Process *right = rhs;
    return left->pid != right->pid;
}


void init_scheduler() {
    init_queue(&run, sizeof(Process), FALSE, process_compare, FALSE);
    init_queue(&io, sizeof(Process), FALSE, process_compare, FALSE);
}


void init_process(Process *process) {
    process->pid = 0;
    process->units = 0;
    process->progress = 0;
    process->promotion = 0;
    process->demotion = 0;
    init_queue(&process->behaviours, sizeof(Behaviour), TRUE, NULL, TRUE);
}


int scheduler_is_active() {
    return (queue_length(&run) > 0) || (queue_length(&io) > 0) || (queue_length(&pending) > 0);
}


void load_process_descriptions(FILE* input) {
    unsigned int arrival;
    int previous_pid = 0;
    Behaviour behaviour;
    Process process;

    init_queue(&pending, sizeof(Process), FALSE, process_compare, FALSE);
    init_process(&process);

    while (fscanf(input, "%u", &arrival) != EOF) {
        fscanf(input, "%d %u %u %u\n", &process.pid, &behaviour.cpu_time, &behaviour.io_time, &behaviour.repeats);

        if (previous_pid == process.pid) {
            add_to_queue(&pending, &process, arrival);
            init_process(&process);
        }
        add_to_queue(&process.behaviours, &behaviour, 1);
        previous_pid = process.pid;
    }
    add_to_queue(&pending, &process, arrival);
}


void queue_new_processes() {
    Process process;

    // schedule pending processes.
    while (queue_length(&pending) && current_priority(&pending) >= mlqfs_clock) {
        remove_from_front(&pending, &process);
        add_to_queue(&run, &process, MAX_PRIORITY);
    }

    // return io processes to cpu.
    while (queue_length(&io) && current_priority(&io) >= mlqfs_clock) {
        remove_from_front(&io, &process);
        add_to_queue(&run, &process, process.priority_cache);
    }
}


void send_process_to_io() {
    Process process;
    Behaviour behaviour;
    int priority = current_priority(&run);
    remove_from_front(&run, &process);
    peek_at_current(&process.behaviours, &behaviour);

    process.promotion ++;
    process.demotion = 0;

    if (process.promotion >= PROMOTION[priority]) {
        process.promotion = 0;
        if (priority != MAX_PRIORITY) { priority --; }
    }

    // store priority in the process struct.
    process.priority_cache = priority;

    process.progress ++;
    process.units = 0;

    add_to_queue(&io, &process, mlqfs_clock + behaviour.io_time);
    fprintf(stdout, "I/O:\tProcess %d blocked for I/O at time %u.\n", process.pid, mlqfs_clock);
}


void halt_process() {
    Process process;
    int priority = current_priority(&run);
    remove_from_front(&run, &process);
    process.demotion ++;
    process.promotion = 0;

    if (process.demotion >= DEMOTION[priority]) {
        process.demotion = 0;
        if (priority != MIN_PRIORITY) { priority ++; }
    }

    add_to_queue(&run, &process, priority);
    fprintf(stdout, "QUEUED:\tProcess %d queued at level %d at time %u.\n", process.pid, priority, mlqfs_clock);
}


void schedule_processes() {
    if (queue_length(&run) == 0) {
        // nothing to schedule.
        return;
    }

    Process process;
    Behaviour behaviour;
    int priority, rescheduling = TRUE;

    // looks at the top process, and while it's not eligible for a cpu unit, it will be rescheduled.
    if (rescheduling) {
        peek_at_current(&run, &process);
        priority = current_priority(&run);
        peek_at_current(&process.behaviours, &behaviour);

        // Process has finished its current behaviour
        if (process.progress >= behaviour.repeats) {
            remove_from_front(&process.behaviours, &behaviour);
            process.progress = 0;
            update_current(&run, &process);
        }

        // Process should be terminated
        if (queue_length(&process.behaviours) == 0 && process.units >= 1) {
            remove_from_front(&run, &process);
            fprintf(stdout, "FINISHED:\tProcess %d finished at time %u.\n", process.pid, mlqfs_clock);
            return;
        }

        peek_at_current(&process.behaviours, &behaviour);

        // Process has finished its burst
        if (process.units >= behaviour.cpu_time) {
            send_process_to_io();
        }

        // Process has consumed its quantas
        else if (process.units > 0 && process.units % QUANTUM[priority] == 0) {
            halt_process();
        }

        else {
            // process is starting a new cpu cycle
            if (process.units == 0) {
                fprintf(stdout, "RUN:\tProcess %d started execution from level %d at time %u; wants to execute for %u ticks.\n", process.pid, priority, mlqfs_clock, behaviour.cpu_time);
            }
            rescheduling = FALSE;
        }
    }
}


int run_top_process() {
    if (queue_length(&run) == 0) {
        // Run null process
        return 0;
    }

    Process current;
    peek_at_current(&run, &current);

    current.units ++;

    update_current(&run, &current);
    return current.pid;
}

int main(int argc, const char * argv[]) {
    FILE* input = argc < 2 ? stdin : fopen(argv[1], "r");

    init_scheduler();
    load_process_descriptions(input);

    while (scheduler_is_active()) {
        queue_new_processes();
        run_top_process();
        schedule_processes();
        mlqfs_clock ++;
    }

    return 0;
}


