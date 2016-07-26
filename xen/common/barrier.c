/*
 * barrier.c
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

#include <xen/barrier.h> 
#include <asm/processor.h>
#include <asm/system.h>

void init_barrier(barrier_t* b, unsigned int init_count) {
    spin_lock_init(&b->lock);
    b->init_count = init_count;
    b->current_count = init_count;
    b->ready = 0;
}

void reset_barrier(barrier_t* b) {
    b->current_count = b->init_count;
}

void waiton_barrier_with_count(barrier_t* b, unsigned int count) {
    unsigned long flags;
    unsigned char local_ready;

    ASSERT(b->init_count >= count);

    local_ready = b->ready;

    spin_lock_irqsave(&b->lock, flags);
    b->current_count--;
    if (b->current_count > (b->init_count - count)) {
        spin_unlock_irqrestore(&b->lock, flags);
        while (b->ready == local_ready) {
            cpu_relax();
        }
    }
    else {
        spin_unlock_irqrestore(&b->lock, flags);
        reset_barrier(b);
        wmb();
        b->ready++;
    }
}

