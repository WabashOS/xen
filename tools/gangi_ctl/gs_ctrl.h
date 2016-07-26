/*
 * gs_ctrl.h
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
 * Includes functions to set and get parameters of the gang-scheduler domains. 
 */

#ifndef GS_CTRL_H
#define GS_CTRL_H

#include <xenctrl.h>

/** 
 * Sets the configuration parameters for the specified gang-scheduled domains. 
 * @param[in] xch Xen Control Interface handle. 
 * @param[in] cpupool_id identifier of the CPU pool.
 * @param[in] params configuration parameters for domains.
 * @return 0 if successful, otherwise an error. 
 */
int gs_params_set(xc_interface* xch, 
                  uint32_t cpupool_id,
                  gang_sched_params_t* params);

 
/** 
 * Returns the current configuration parameters for the specified gang-scheduled
 * domains. 
 * @param[in] xch Xen Control Interface handle. 
 * @param[in] cpupool_id identifier of the CPU pool.
 * @param[out] params configuration parameters of domains. 
 *                    Must be freed by the caller.
 * @return 0 if successful, otherwise an error. 
 */
int gs_params_get(xc_interface* xch,
                  uint32_t cpupool_id,
                  gang_sched_params_t** params);


/** Allocates a gang_sched_params_t structure. */
gang_sched_params_t* alloc_gang_sched_params(void);


/** 
 * Frees the given gang_sched_params_t structure. 
 * Also frees the array of CPU IDs in each configuraion domain entry.
 */
void free_gang_sched_params(gang_sched_params_t* p);


#endif
