/**
 *  main.c
 *  mlqfs
 *
 *  Created by Pierre Gabory on 24/10/2019.
 *  Copyright © 2019 piergabory. All rights reserved.
 */

#include "mlqfs.h"

#define MAX_PRIORITY 0
#define MIN_PRIORITY 2

static const int QUANTUM_THRESHOLD[3] = { 10, 30, 100 };
static const int DEMOTION_THRESHOLD[3] = { 1, 2, 'X' };
static const int PROMOTION_THRESHOLD[3] = { 'X', 2, 1 };

static Queue ready_queue;       // Processes waiting for CPU time.
static Queue io_queue;        // Processes in IO. 
static Queue arrival_queue;   // Processes waiting for their arrival time.
static Queue logs;    // Terminated processes buffer, used in the report output.

// Define the null process used in the report output.
static Process null = { .pid = 0, .total_cpu_usage = 0 }; 

// Clock counter
static unsigned int mlqfs_clock = 0;

// Stream output
static FILE* output = NULL;

static Process running;


/**
 * @brief compare two processes struct
 * Required generic comparison function for the Queue struct,
 * used to check for duplicates in the queue.
 * Duplicates processes are identified by PID
 *
 * @param lhs void pointer castable to Process pointer.
 * @param rhs void pointer castable to Process pointer.
 * @returns 1 if the processes are different 0 if they're the same.
 */
int process_compare(void* lhs, void* rhs) {
    Process *left = lhs;
    Process *right = rhs;
    return left->pid != right->pid;
}


/**
 * @brief MLQFScheduler initializer
 * call initializer function for each queues
 * representing the state of the scheduler
 */
void init_scheduler() {
    init_queue(&ready_queue, sizeof(Process), FALSE, process_compare, FALSE);
    init_queue(&io_queue, sizeof(Process), FALSE, process_compare, FALSE);
    init_queue(&logs, sizeof(Process), FALSE, process_compare, FALSE);
}


/**
 * @brief Shutdown the MLQFScheduler
 * free the memory for all the scheduler state queues.
 * saves the null process logs.
 * logs the shutdown time.
 */
void shutdown_scheduler() {
    destroy_queue(&ready_queue);
    destroy_queue(&io_queue);
    destroy_queue(&arrival_queue);

    // add NULL process to the record if it was ever spawned.
    if (null.total_cpu_usage > 0) {
        add_to_queue(&logs, &null, null.total_cpu_usage);
    }

    fprintf(output, "Scheduler shutdown at time %u.\n", mlqfs_clock);
}


/**
 * @brief Initialise Process
 * Sets all the propreties of a process struct to their default values.
 * Initialise the processe's behavior queue.
 */
void init_process(Process *process) {
    process->pid = 0;
    process->priority_cache = 0;
    process->arrival_time = 0;
    process->units = 0;
    process->quanta = 0;
    process->progress = 0;
    process->promotion = 0;
    process->demotion = 0;
    process->total_cpu_usage = 0;

    init_queue(&process->behaviours, sizeof(Behaviour), TRUE, NULL, FALSE);
}


/**
 * @brief Check the activity of the MLQFScheduler
 * the scheduler is considered active as long as there
 * is an active process running, waiting io, or waiting to start.
 * @return boolean
 */
int scheduler_is_active() {
    return (queue_length(&ready_queue) > 0) || (queue_length(&io_queue) > 0) || (queue_length(&arrival_queue) > 0);
}


/**
 * @brief Load process descriptions
 * Parses a character stream into a queue of Processes.
 * A process is describe with 5 space separated integers:
 * "[Arrival_time] [PID] [cpu_time] [io_time] [repeats]"
 * pushes all the new processes in the arrival queue.
 *
 * @param input stream containing the processes descriptions.
 */
void load_process_descriptions(FILE* input) {
    Process process;
    Behaviour behaviour;
    int pid = 0, is_first = TRUE;
    unsigned int arrival;

    init_process(&process);
    init_queue(&arrival_queue, sizeof(Process), FALSE, process_compare, FALSE);
    arrival = 0;

    while (fscanf(input, "%u", &arrival) != EOF) {
        fscanf(input, "%d %d %d %d", &pid, &behaviour.cpu_time, &behaviour.io_time, &behaviour.repeats);

        if (!is_first && process.pid != pid) {
            add_to_queue(&arrival_queue, &process, process.arrival_time);
            init_process(&process);
        }

        process.pid = pid;
        process.arrival_time = arrival;
        is_first = FALSE;
        add_to_queue(&process.behaviours, &behaviour, 1);
    }

    add_to_queue(&arrival_queue, &process, process.arrival_time);
}


/**
 * @brief Queue processes to CPU
 * At current clock time, pull all the processes from the arrival and
 * io queue and push them in the ready queue.
 * New processes are set with the highest priority by default.
 * Processes leaving io return to their previous priority stored in
 * the priority_cache property.
 */
void queue_new_processes() {
    Process process, previous_active = null;

    // save current active process pid
    if (queue_length(&ready_queue) > 0) {
        peek_at_current(&ready_queue, &previous_active);
        previous_active.priority_cache = current_priority(&ready_queue);
    }

    // schedule arrival processes.
    while (queue_length(&arrival_queue) > 0 && current_priority(&arrival_queue) <= mlqfs_clock) {
        remove_from_front(&arrival_queue, &process);
        add_to_queue(&ready_queue, &process, MAX_PRIORITY);
        fprintf(output, "CREATE: Process %d entered the ready queue at time %d.\n", process.pid, mlqfs_clock);
    }

    // return io processes to cpu.
    while (queue_length(&io_queue) > 0 && current_priority(&io_queue) <= mlqfs_clock) {
        remove_from_front(&io_queue, &process);
        add_to_queue(&ready_queue, &process, process.priority_cache);
        // log queueing when leaving io
        fprintf(output, "QUEUED: Process %d queued at level %d at time %u.\n", process.pid, process.priority_cache + 1, mlqfs_clock);
    }

    // log preemption
    if (queue_length(&ready_queue) > 0 && previous_active.pid != null.pid) {
        peek_at_current(&ready_queue, &process);
        if (previous_active.pid != process.pid) {
            fprintf(output, "QUEUED: Process %d queued at level %d at time %u.\n", previous_active.pid, previous_active.priority_cache + 1, mlqfs_clock);
        }
    }
}


/**
 * @brief Send top process to io
 * Remove the current process form the ready queue and pushes it in the io queue.
 * Resets quanta and unit counters, and increment progress and promotion counters.
 * If the promotion counter reaches the priority's Promotion ceiling, the process
 * is promoted to the next highest priority.
 * Print the IO log in the output stream.
 */
void send_process_to_io() {
    Process process;
    Behaviour behaviour;
    int priority = current_priority(&ready_queue);
    remove_from_front(&ready_queue, &process);
    peek_at_current(&process.behaviours, &behaviour);

    process.promotion ++;
    process.demotion = 0;

    // promote process
    if (process.promotion >= PROMOTION_THRESHOLD[priority]) {
        process.promotion = 0;
        if (priority != MAX_PRIORITY) { priority --; }
    }

    // store priority in the process struct.
    process.priority_cache = priority;

    process.progress ++;
    process.units = 0;
    process.quanta = 0;

    add_to_queue(&io_queue, &process, mlqfs_clock + behaviour.io_time);
    fprintf(output, "I/O: Process %d blocked for I/O at time %u.\n", process.pid, mlqfs_clock);
}


/**
 * @brief Stop the currently running process.
 * Called when the process runs out of quantum.
 * Stops the currently run process and put it at the end of the queue.
 * the demotion counter is incremented, and the process is demoted a priority
 * if it reaches the priority's demotion ceiling.
 */
void halt_process() {
    Process process;
    int priority = current_priority(&ready_queue);
    remove_from_front(&ready_queue, &process);
    process.demotion ++;
    process.promotion = 0;
    process.quanta = 0;

    // demote process
    if (process.demotion >= DEMOTION_THRESHOLD[priority]) {
        process.demotion = 0;
        if (priority != MIN_PRIORITY) { priority ++; }
    }

    add_to_queue(&ready_queue, &process, priority);
    fprintf(output, "QUEUED: Process %d queued at level %d at time %u.\n", process.pid, priority + 1, mlqfs_clock);
}


/**
 * @brief Terminate currently running process
 * called when the current process has finished all its cpu cycles.
 * Removes the process from the ready queue and free the behaviour queue.
 */
void terminate_process() {
    Process process;
    remove_from_front(&ready_queue, &process);
    destroy_queue(&process.behaviours);
    add_to_queue(&logs, &process, process.total_cpu_usage);
    fprintf(output, "FINISHED: Process %d finished at time %u.\n", process.pid, mlqfs_clock);
}


/**
 * @brief Updates the ready queue so the top process is the one who deserves CPU access the most.
 */
void schedule_processes() {
    Process process;
    Behaviour behaviour;
    int priority;

    // looks at the top process, and while it's not eligible for a cpu unit, it will be rescheduled.
    while (queue_length(&ready_queue) > 0) {
        peek_at_current(&ready_queue, &process);
        priority = current_priority(&ready_queue);
        peek_at_current(&process.behaviours, &behaviour);

        // Process should be terminated
        // (process is on its last cycle and as finished the extra CPU run.
        if (queue_length(&process.behaviours) == 1 && process.progress == behaviour.repeats && process.units >= behaviour.cpu_time) {
            terminate_process();
        }


        // Process finished its current behaviour description.
        // Ignored if process is on its last behaviour
        else if (queue_length(&process.behaviours) > 1 && process.progress >= behaviour.repeats) {
            remove_from_front(&process.behaviours, &behaviour);
            process.progress = 0;
            update_current(&ready_queue, &process);
        }


        // Process has finished its burst
        else if (process.units >= behaviour.cpu_time) {
            send_process_to_io();
        }


        // Process is elegible for cpu access.
        else {
            // process is starting a new cpu cycle
            if (process.quanta == 0 || process.pid != running.pid) {
                int time_left = behaviour.cpu_time - process.units;
                fprintf(output, "RUN: Process %d started execution from level %d at time %u; wants to execute for %u ticks.\n", process.pid, priority + 1, mlqfs_clock, time_left);
            }
            running = process;
            return;
        }
    }

    running = null;
}


/**
 * @brief simulate the top process cpu access.
 * If no process are scheduled, will run the NULL process.
 * Increments unit, quanta, and total cpu usage counters.
 */
void run_top_process() {
    if (queue_length(&ready_queue) == 0) {
        // Run null process
        null.total_cpu_usage ++;
    }

    else {
        // get process
        Process process;
        peek_at_current(&ready_queue, &process);

        // Update counters
        process.units ++;
        process.total_cpu_usage ++;

        // save changes
        update_current(&ready_queue, &process);
    }
}


void check_top_process_quanta() {
    if (queue_length(&ready_queue) == 0) {
        // No quantum limits on null process.
        return;
    }


    // get process
    Process process;
    peek_at_current(&ready_queue, &process);
    process.quanta ++;
    update_current(&ready_queue, &process);
    int priority = current_priority(&ready_queue);

    // Process has consumed its quanta
    if (process.quanta >= QUANTUM_THRESHOLD[priority]) {
        halt_process();
    }
}


/**
 * @brief prints formated output of total cpu usage of every processes, NULL inluded.
 */
void print_report() {
    Process process;
    fprintf(output, "\nTotal CPU usage for all processes scheduled:\n\n");
    while (queue_length(&logs) != 0) {
        fprintf(output, "Process ");
        remove_from_front(&logs, &process);
        switch (process.pid) {
            case 0: fprintf(output, "<<null>> "); break;
            default: fprintf(output, "%d ", process.pid); break;
        }
        fprintf(output, ": %d time units.\n", process.total_cpu_usage);
    }
    destroy_queue(&logs);
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
        output = fopen(argv[2], "w");
    } else {
        output = stdout;
    }



    // --- BEGIN SCHEDULER ---
    init_scheduler();

    mlqfs_clock = 0;
    while (scheduler_is_active()) {
        check_top_process_quanta();
        queue_new_processes();
        schedule_processes();
        run_top_process();
        mlqfs_clock ++;
    }
    mlqfs_clock --;
    shutdown_scheduler();
    // --- END SCHEDULER ---

    print_report();

    if (argc >= 3) { fclose(output); }
    return 0;
}
