//
//  mlqfs.h
//  mlqfs
//
//  Created by Pierre Gabory on 24/10/2019.
//  Copyright © 2019 piergabory. All rights reserved.
//

#ifndef mlqfs_h
#define mlqfs_h

#include <stdio.h>
#include "prioque.h"

typedef struct Process {
    int pid;
    int priority_cache;
    Queue behaviours;
    int units;
    int progress;
    int promotion;
    int demotion;
} Process;

typedef struct Behaviour {
    unsigned int cpu_time;
    unsigned int io_time;
    unsigned int repeats;
} Behaviour;

int process_compare(void* lhs, void* rhs);

int scheduler_is_active(void);

#endif /* mlqfs_h */
