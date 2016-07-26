/*
 * gs_utils.h
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
 * Copyright (C) 2014, Juan A. Colmenares <juancol@eecs.berkeley.edu>
 */

/**
 * Includes utility functions.
 */

#ifndef GS_UTILS_H
#define GS_UTILS_H

#include <xenctrl.h>
#include "gs_ctrl.h"

/** Tells whether or not the CPU pool is valid. */
int validate_cpupool(xc_interface* xch, uint32_t cpupool_id);


/** Returns the number of domains in the given CPU pool. */
int num_of_domains_in_cpupool(xc_interface* xch, uint32_t cpupool_id);


/** Tells whether or not all the domain ID is valid. */
int validate_domain_id(xc_interface* xch, 
                       uint32_t cpupool_id,
                       uint32_t dom_id);

/** Tells whether or not the gang-scheduling control parameters are valid. */
int validate_params(xc_interface* xch, 
                    uint32_t cpupool_id,
                    gang_sched_params_t* params);

/** Prints gang-scheduling parameters of existing domains to console. */
void print_gang_sched_params(gang_sched_params_t* params);

/** Prints the gang-scheduling policy to console. */
void print_gang_sched_policy(gang_sched_policy_t* policy);

#endif
