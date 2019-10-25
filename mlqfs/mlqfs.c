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
static Queue report;

static Process null = { .pid = 0, .total_cpu_usage = 0 };

static unsigned int mlqfs_clock = 0;
static FILE* output = NULL;

int process_compare(void* lhs, void* rhs) {
    Process *left = lhs;
    Process *right = rhs;
    return left->pid != right->pid;
}

void init_scheduler() {
    init_queue(&run, sizeof(Process), FALSE, process_compare, FALSE);
    init_queue(&io, sizeof(Process), FALSE, process_compare, FALSE);
    init_queue(&report, sizeof(Process), FALSE, process_compare, FALSE);
}

void shutdown_scheduler() {
    destroy_queue(&run);
    destroy_queue(&io);
    destroy_queue(&run);

    // add NULL process to the record if it was ever spawned.
    if (null.total_cpu_usage > 0) {
        add_to_queue(&report, &null, null.total_cpu_usage);
    }

    fprintf(output, "Scheduler shutdown at time %u.\n", mlqfs_clock);
}

void init_process(Process *process) {
    process->pid = 0;
    process->priority_cache = 0;
    process->arrival_time = 0;
    process->units = 0;
    process->quantas = 0;
    process->progress = 0;
    process->promotion = 0;
    process->demotion = 0;
    process->total_cpu_usage = 0;

    init_queue(&process->behaviours, sizeof(Behaviour), TRUE, NULL, TRUE);
}


int scheduler_is_active() {
    return (queue_length(&run) > 0) || (queue_length(&io) > 0) || (queue_length(&pending) > 0);
}


void load_process_descriptions(FILE* input) {
    Process process;
    Behaviour behaviour;
    int pid = 0, is_first = TRUE;
    unsigned int arrival;

    init_process(&process);
    init_queue(&pending, sizeof(Process), FALSE, process_compare, FALSE);
    arrival = 0;

    while (fscanf(input, "%u", &arrival) != EOF) {
        fscanf(input, "%d %d %d %d", &pid, &behaviour.cpu_time, &behaviour.io_time, &behaviour.repeats);

        if (!is_first && process.pid != pid) {
            add_to_queue(&pending, &process, process.arrival_time);
            init_process(&process);
        }

        process.pid = pid;
        process.arrival_time = arrival;
        is_first = FALSE;
        add_to_queue(&process.behaviours, &behaviour, 1);
    }

    add_to_queue(&pending, &process, process.arrival_time);
}


void queue_new_processes() {
    Process process;

    // schedule pending processes.
    while (queue_length(&pending) > 0 && current_priority(&pending) <= mlqfs_clock) {
        remove_from_front(&pending, &process);
        add_to_queue(&run, &process, MAX_PRIORITY);
    }

    // return io processes to cpu.
    while (queue_length(&io) > 0 && current_priority(&io) <= mlqfs_clock) {
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
    process.quantas = 0;

    add_to_queue(&io, &process, mlqfs_clock + behaviour.io_time);
    fprintf(output, "I/O:\tProcess %d blocked for I/O at time %u.\n", process.pid, mlqfs_clock);
}


void halt_process() {
    Process process;
    int priority = current_priority(&run);
    remove_from_front(&run, &process);
    process.demotion ++;
    process.promotion = 0;
    process.quantas = 0;

    if (process.demotion >= DEMOTION[priority]) {
        process.demotion = 0;
        if (priority != MIN_PRIORITY) { priority ++; }
    }

    add_to_queue(&run, &process, priority);
    fprintf(output, "QUEUED:\tProcess %d queued at level %d at time %u.\n", process.pid, priority, mlqfs_clock);
}

void terminate_process() {
    Process process;
    remove_from_front(&run, &process);
    destroy_queue(&process.behaviours);
    add_to_queue(&report, &process, process.total_cpu_usage);
    fprintf(output, "FINISHED:\tProcess %d finished at time %u.\n", process.pid, mlqfs_clock);
}


void schedule_processes() {
    Process process;
    Behaviour behaviour;
    int priority;

    // looks at the top process, and while it's not eligible for a cpu unit, it will be rescheduled.
    while (queue_length(&run) > 0) {
        peek_at_current(&run, &process);
        priority = current_priority(&run);
        
        // Process should be terminated
        if (queue_length(&process.behaviours) == 0) {
            // if ready to terminate, let the process run one more cpu cycle.
            if (process.units < 1) { return; }

            // else remove the process and try with the next one.
            terminate_process();
            continue;
        }
        
        peek_at_current(&process.behaviours, &behaviour);

        // Process has finished its current behaviour
        if (process.progress >= behaviour.repeats) {
            remove_from_front(&process.behaviours, &behaviour);
            process.progress = 0;
            update_current(&run, &process);
        }


        // Process has finished its burst
        else if (process.units >= behaviour.cpu_time) {
            send_process_to_io();
        }


        // Process has consumed its quantas
        else if (process.quantas >= QUANTUM[priority]) {
            halt_process();
        }


        // Process is elegible for cpu access.
        else {
            // process is starting a new cpu cycle
            if (process.units == 0) {
                fprintf(output, "RUN:\tProcess %d started execution from level %d at time %u; wants to execute for %u ticks.\n", process.pid, priority, mlqfs_clock, behaviour.cpu_time);
            }
            return;
        }
    }
}


int run_top_process() {
    if (queue_length(&run) == 0) {
        // Run null process
        null.total_cpu_usage ++;
        return 0;
    }

    Process current;
    peek_at_current(&run, &current);

    current.units ++;
    current.quantas ++;
    current.total_cpu_usage ++;

    update_current(&run, &current);
    return current.pid;
}


void print_report() {
    Process process;
    fprintf(output, "\nTotal CPU usage for all processes scheduled:\n\n");
    while (queue_length(&report) != 0) {
        fprintf(output, "Process ");
        remove_from_front(&report, &process);
        switch (process.pid) {
            case 0: fprintf(output, "<<null>> "); break;
            default: fprintf(output, "%d ", process.pid); break;
        }
        fprintf(output, ":\t%d time units.\n", process.total_cpu_usage);
    }
    destroy_queue(&report);
}


int main(int argc, const char * argv[]) {
    // used for convinient debugging in my IDE
    if (argc >= 2) {
        FILE* input = fopen(argv[1], "r");
        load_process_descriptions(input);
        fclose(input);
    } else {
        load_process_descriptions(stdin);
    }

    if (argc >= 3) {
        output = fopen("mlqfs_report.txt", "w");
    } else {
        output = stdout;
    }

    init_scheduler();

    mlqfs_clock = 0;
    while (scheduler_is_active()) {
        queue_new_processes();
        schedule_processes();
        run_top_process();
        mlqfs_clock ++;
    }

    shutdown_scheduler();
    print_report();

    if (argc >= 3) { fclose(output); }

    return 0;
}


