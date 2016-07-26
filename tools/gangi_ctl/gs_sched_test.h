/*
 * gs_sched_test.h
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
 * Copyright (C) 2015, Juan A. Colmenares <juancol@eecs.berkeley.edu>
 */


/**
 * Implements a schedulability test for the integrated gang scheduling algorithm. 
 */

#ifndef GS_SCHED_TEST_H
#define GS_SCHED_TEST_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <xen/gang_sched_policies.h>

const uint64_t INFINITY = UINT64_MAX;


/** Represents a domain. */
typedef struct GsDomainInfo {

    /** Domain ID. */
    int32_t domid;

    /** Array with IDs of the assigned CPUs for the domain. */
    int32_t* cpus; 
       
    /** Number of element in the array of assigned CPUs. */
    int32_t num_of_cpus;

    /** Specification of the gang-scheduling policy for the domain. */
    gang_sched_policy_t gang_sched_policy;

} gs_dominfo_t;


/**
 * Determines whether or not the domains are schedulable by the gang scheduling
 * algorithm.
 * @param arr array of pointers to domain info items. 
 * @param len number of items in the array.
 * @param be_reserve Collective CPU reservation (in percentage) for best-effort domains.
 *                   Valid values are [0, 100].
 * @param be_basic_period Basic period for best-effort domains (in milliseconds).
 * @param cpu_count Number of CPUs in the system.
 * @return 0 if successful; 1 otherwise. A negative number indicated an error. 
 */
int are_schedulable(gs_dominfo_t** arr, 
                    size_t len, 
                    size_t be_reserve, 
                    uint64_t be_basic_period, 
                    size_t cpu_count);

#ifdef __cplusplus
}
#endif


#define SUPPRESS_NOT_USED_WARN __attribute__((unused))


#endif
