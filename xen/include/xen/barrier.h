/*
 * barrier.h
 *
 * Simple spinlock based barrier.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Originally implemented by: 
 * - Barret Rhoden <brho@eecs.berkeley.edu> 
 * - Kevin Klues <klueska@eecs.berkeley.edu>
 *
 * Extended by:
 * - Juan A. Colmenares <juancol@eecs.berkeley.edu>
 * 
 * Copyright (c) 2009 Barret Rhoden <brho@eecs.berkeley.edu> 
 * Copyright (c) 2009 Kevin Klues <klueska@eecs.berkeley.edu>
 * Copyright (c) 2010 Juan A. Colmenares <juancol@eecs.berkeley.edu>
 * Copyright (c) 2009, 2010 The Regents of the University of California.
 */


#ifndef __SPINLOCK_BASED_BARRIER_H__
#define __SPINLOCK_BASED_BARRIER_H__

#include <xen/lib.h>
#include <xen/spinlock.h>

/** Barrier. */
typedef struct barrier {
    spinlock_t lock;
    unsigned int init_count;
    unsigned int current_count;
    volatile unsigned char ready;
} barrier_t;


/**
 * Initializes the barrier.
 * @param barrier the barrier.
 * @param init_count initial count.
 */
void init_barrier(barrier_t* b, unsigned int init_count);

/** Resets the barrier to its initial count. */
void reset_barrier(barrier_t* b);


/**
 * Waits on the barrier for the "count" threads of execution to arrive.
 * @param barrier
 * @param count number of execution threads must hit the barrier to continue.
 *              It must be lesser than or equal to the barrier's initial count.
 *              Also, to work properly, each of the 'count' calling this
 *              function must pass the same 'count' value.
 */
void waiton_barrier_with_count(barrier_t* b, unsigned int count);


/** Waits on the barrier for everybody in the initial count to arrive. */
static always_inline void waiton_barrier(barrier_t* b) {
    waiton_barrier_with_count(b, b->init_count);
}

#endif
