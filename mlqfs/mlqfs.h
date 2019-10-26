/**
 *  mlqfs.h
 *  mlqfs
 *
 *  Created by Pierre Gabory on 24/10/2019.
 *  Copyright © 2019 piergabory. All rights reserved.
 */

#ifndef mlqfs_h
#define mlqfs_h

#include <stdio.h>
#include "prioque.h"

typedef struct Process {
    int pid;
    int priority_cache;
    Queue behaviours;
    unsigned int arrival_time;
    unsigned int units;
    unsigned int quantas;
    unsigned int progress;
    unsigned int promotion;
    unsigned int demotion;
    unsigned int total_cpu_usage;
} Process;

typedef struct Behaviour {
    unsigned int cpu_time;
    unsigned int io_time;
    unsigned int repeats;
} Behaviour;

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
int process_compare(void* lhs, void* rhs);

/**
 * @brief MLQFScheduler initializer
 * call initializer function for each queues
 * representing the state of the scheduler
 */
void init_scheduler(void);

/**
 * @brief Shutdown the MLQFScheduler
 * free the memory for all the scheduler state queues.
 * saves the null process logs.
 * logs the shutdown time.
 */
void shutdown_scheduler(void);

/**
 * @brief Check the activity of the MLQFScheduler
 * the scheduler is considered active as long as there
 * is an active process running, waiting io, or waiting to start.
 * @return boolean
 */
int scheduler_is_active(void);

/**
 * @brief Queue processes to CPU
 * At current clock time, pull all the processes from the pending and
 * io queue and push them in the run queue.
 * New processes are set with the highest priority by default.
 * Processes leaving io return to their previous priority stored in
 * the priority_cache property.
 */
void queue_new_processes(void);

/**
 * @brief Updates the run queue so the top process is the one who deserves CPU access the most.
 */
void schedule_processes(void);

/**
 * @brief simulate the top process cpu access.
 * If no process are scheduled, will run the NULL process.
 * Increments unit, quanta, and total cpu usage counters.
 */
void run_top_process(void);

/**
 * @brief prints formated output of total cpu usage of every processes, NULL inluded.
 */
void print_report(void);


int main(int argc, const char * argv[]);

#endif /* mlqfs_h */
