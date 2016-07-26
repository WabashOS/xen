/*
 * gs_ctrl.c
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


#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>

#include <xenctrl.h>

#include "gs_ctrl.h"
#include "gs_utils.h"


int gs_params_set(xc_interface* xch, 
                  uint32_t cpupool_id,
                  gang_sched_params_t* params) {
    int rc = 0;

    assert(xch != NULL);
    assert(params != NULL);

    rc = validate_cpupool(xch, cpupool_id);
    if (rc) {
        fprintf(stderr, "validate_cpupool(...) failed! Error code: %d\n", rc);
        return rc;   
    } 

    rc = validate_params(xch, cpupool_id, params);
    if (rc) {
        fprintf(stderr, "validate_params(...) failed! Error code: %d\n", rc);
        return rc;   
    } 

    rc = xc_sched_gang_params_set(xch, cpupool_id, params);

    return rc;
}

 
int gs_params_get(xc_interface* xch,
                  uint32_t cpupool_id,
                  gang_sched_params_t** params) {
    int rc = 0;
    gang_sched_params_t* p;

    assert(xch != NULL);
    assert(params != NULL);

    rc = validate_cpupool(xch, cpupool_id);
    if (rc) {
        fprintf(stderr, "validate_cpupool(...) failed! Error code: %d\n", rc);
        return rc;   
    } 
    
    p = alloc_gang_sched_params();
    assert(p != NULL);

    rc = xc_sched_gang_params_get(xch, cpupool_id, p);
    if (rc) {
        free_gang_sched_params(p);
    }
    else {
        *params = p;
    }

    return rc;
}


gang_sched_params_t* alloc_gang_sched_params(void) {
    return (gang_sched_params_t*) calloc(1, sizeof(gang_sched_params_t));
}


void free_gang_sched_params(gang_sched_params_t* p) {

    for (int i = 0; i < GANG_SCHED_MAX_DOMAINS; i++) {
        if (p->dom_entries[i].cpus != NULL) {
            free(p->dom_entries[i].cpus);
            p->dom_entries[i].cpus = NULL;
        }
    }

   free(p); 
}


