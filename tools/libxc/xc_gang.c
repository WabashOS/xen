/****************************************************************************
 * File: xc_gang.c
 *
 * Description: XC Interface to the Gang Scheduler.
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
 * Author: Juan A. Colmenares <juan.col@eecs.berkeley.edu>
 *         Based on xc_arinc653.c
 *
 * Copyright (c) 2014, Juan A. Colmenares <juancol@eecs.berkeley.edu>
 */


#include "xc_private.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>


#ifndef SUPPRESS_NOT_USED_WARN
    #define SUPPRESS_NOT_USED_WARN __attribute__((unused))
#endif

 
// Note: 
// The following domain-specific control functions are not provided:
//
// int xc_sched_gang_domain_set(xc_interface* xch, 
//                              uint32_t domid,
//                              gang_sched_spec_t* sched_params);
// 
// int xc_sched_gang_domain_get(xc_interface* xch,
//                              uint32_t domid,
//                              gang_sched_spec_t* sched_params);
// 
// Instead, we only offer system-wide control functions. The reason is that the
// system-wide control functions can offer same functionality as that of the
// above domain control functions. In other words, xc_sched_gang_params_set(...)
// can set the scheduling parameters and CPUs of 1 or more domains. Similarly,
// xc_sched_gang_params_get(...) can return the current configuration parameters
// of 1 or more domains. 
// 


/**
 * Sets the bits in the bitmap that corresponds to the integers in the array.
 */
static SUPPRESS_NOT_USED_WARN 
void array_to_bitmap(int32_t* arr, 
                     int32_t arr_len, 
                     uint8_t* bitmap, 
                     int32_t nr_bits) {
    assert(arr != NULL);
    assert(arr_len >= 0);
    assert(bitmap != NULL);
    assert(nr_bits > 0);

    assert(arr_len <= nr_bits);

    for (int32_t i = 0; i < arr_len; i++) {
        int byte, bit;

        assert(arr[i] >= 0);
        assert(arr[i] < nr_bits);

        byte = arr[i] / 8;
        bit = arr[i] % 8; 
        bitmap[byte] |= (1 << bit);
    }
} 

/**
 * Populates the array with the elements that correspond to the set bits in the bitmap.
 * @returns the number of bits set in the bitmap, that equals to the number of
 *          elements also set in the output array.
 */
static SUPPRESS_NOT_USED_WARN 
size_t bitmap_to_array(uint8_t* bitmap,
                       int32_t nr_bits,
                       int32_t* arr,
                       int32_t arr_len) {
    size_t nr_set_bits = 0;

    assert(arr != NULL);
    assert(arr_len >= 0);
    assert(bitmap != NULL);
    assert(nr_bits > 0);

    assert(arr_len >= nr_bits);

    bzero(arr, (arr_len * sizeof(int32_t)));

    for (int32_t i = 0; i < nr_bits; i++) {
        int byte = i / 8;
        int bit = i % 8;

        if (bitmap[byte] & (1 << bit)) {
           arr[nr_set_bits] = i;
           nr_set_bits++;
        }
    }

    return nr_set_bits;
}

/**
 * Copies the content of a 'gang_sched_params_t' struct into a
 * 'xen_sysctl_gang_schedule_t' struct.
 */
static SUPPRESS_NOT_USED_WARN 
int copy_params_into_sysctl(xc_interface* xch, 
                            gang_sched_params_t* src, 
                            xen_sysctl_gang_schedule_t* dst,
                            xc_hypercall_buffer_array_t* hc_buf_arr) {
    int rc = 0;

    int cpumap_size;
    int max_num_of_cpus;

    assert(src != NULL);
    assert(dst != NULL);
    assert(hc_buf_arr != NULL);


    cpumap_size = xc_get_cpumap_size(xch);
    if (cpumap_size <= 0) {
        PERROR("xc_get_cpumap_size(...) failed!\n");
        return -EFAULT;
    }

    max_num_of_cpus = xc_get_max_cpus(xch);

    assert(max_num_of_cpus > 0);
    assert(max_num_of_cpus <= cpumap_size * 8);


    dst->num_dom_entries = src->num_dom_entries;

    for (uint16_t e = 0; e < src->num_dom_entries; e++) {

        DECLARE_HYPERCALL_BUFFER(uint8_t, cpumap);

        if (src->dom_entries[e].num_of_cpus > max_num_of_cpus) {
            PERROR("Invalid number of CPUs in domain configuration entry %d.\n",
                   e);
            rc = -EINVAL;
            break;
        }

        dst->dom_entries[e].domid = src->dom_entries[e].domid;
        dst->dom_entries[e].gang_sched_policy =
            src->dom_entries[e].gang_sched_policy;
    

        // NOTE: 
        // We assume that the number of elements in the 'hypercall buffer array'
        // is equal to 'src->num_dom_entries'.
        cpumap = xc_hypercall_buffer_array_alloc(xch, 
                                                 hc_buf_arr, 
                                                 e, 
                                                 cpumap,
                                                 cpumap_size);
        if (cpumap == NULL) {
            PERROR("xc_hypercall_buffer_array_alloc(...) failed!\n");
            rc = -ENOMEM;
            break;
        }
        
        array_to_bitmap(src->dom_entries[e].cpus,
                        src->dom_entries[e].num_of_cpus, 
                        (uint8_t*) cpumap, 
                        max_num_of_cpus);

        set_xen_guest_handle(dst->dom_entries[e].cpumap.bitmap, cpumap);

        dst->dom_entries[e].cpumap.nr_bits = cpumap_size * 8;
    
    }

    return rc;
}

/**
 * Resets the given sysctl_params and more importantly allocates its internal
 * CPU bitmaps for all the domain entries.
 */
static SUPPRESS_NOT_USED_WARN
int reset_sysctl_and_alloc_bitmaps(xc_interface* xch,
                                   xen_sysctl_gang_schedule_t* sysctl_params,
                                   xc_hypercall_buffer_array_t* hc_buf_arr) {
    int rc = 0;

    int cpumap_size;
    int max_num_of_cpus;

    assert(sysctl_params != NULL);
    assert(hc_buf_arr != NULL);

    cpumap_size = xc_get_cpumap_size(xch);
    if (cpumap_size <= 0) {
        PERROR("xc_get_cpumap_size(...) failed!\n");
        return -EFAULT;
    }

    max_num_of_cpus = xc_get_max_cpus(xch);

    assert(max_num_of_cpus > 0);
    assert(max_num_of_cpus <= cpumap_size * 8);


    sysctl_params->num_dom_entries = 0;

    for (uint16_t e = 0; e < GANG_SCHED_MAX_DOMAINS; e++) {

        DECLARE_HYPERCALL_BUFFER(uint8_t, cpumap);

        sysctl_params->dom_entries[e].domid = -1;
        sysctl_params->dom_entries[e].gang_sched_policy.type =
            GANG_SCHED_POLICY_NOT_SET;

        // NOTE:
        // We assume that the number of elements in the 'hypercall buffer array'
        // is equal to GANG_SCHED_MAX_DOMAINS.
        cpumap = xc_hypercall_buffer_array_alloc(xch,
                                                 hc_buf_arr,
                                                 e,
                                                 cpumap,
                                                 cpumap_size);
        if (cpumap == NULL) {
            PERROR("xc_hypercall_buffer_array_alloc(...) failed!\n");
            rc = -ENOMEM;
            break;
        }

        bzero(cpumap, cpumap_size);

        set_xen_guest_handle(sysctl_params->dom_entries[e].cpumap.bitmap, cpumap);

        sysctl_params->dom_entries[e].cpumap.nr_bits = cpumap_size * 8;
    }

    return rc;
}


/**
 * Copies the content of a 'xen_sysctl_gang_schedule_t' struct into a
 * 'gang_sched_params_t' struct.
 */
static SUPPRESS_NOT_USED_WARN
int copy_sysctl_into_params(xc_interface* xch,
                            xen_sysctl_gang_schedule_t* src,
                            gang_sched_params_t* dst) {
    int rc = 0;

    int cpumap_size;
    int max_num_of_cpus;

    int32_t* tmp_arr = NULL; // Temporary array.

    // Size of the temporary array, 
    // equal to the number of bits in the CPU bitmap.
    int32_t tmp_arr_len = 0;

    assert(src != NULL);
    assert(dst != NULL);

    cpumap_size = xc_get_cpumap_size(xch);
    if (cpumap_size <= 0) {
        PERROR("xc_get_cpumap_size(...) failed!\n");
        return -EFAULT;
    }

    max_num_of_cpus = xc_get_max_cpus(xch);

    assert(max_num_of_cpus > 0);
    assert(max_num_of_cpus <= cpumap_size * 8);


    tmp_arr_len = cpumap_size * 8;
    tmp_arr = (int32_t*) malloc(tmp_arr_len * sizeof(int32_t));


    dst->num_dom_entries = src->num_dom_entries;

    for (uint16_t e = 0; e < src->num_dom_entries; e++) {

        uint8_t* cpumap;
        size_t nr_set_bits = 0;

        assert(src->dom_entries[e].cpumap.nr_bits == tmp_arr_len);

        get_xen_guest_handle(cpumap, src->dom_entries[e].cpumap.bitmap);

        nr_set_bits =
            bitmap_to_array(cpumap,
                            src->dom_entries[e].cpumap.nr_bits,
                            tmp_arr, // Zeroed inside the function.
                            tmp_arr_len);

        if (nr_set_bits != 0) {
            size_t sz = nr_set_bits * sizeof(int32_t);
            int32_t* cpus = (int32_t*) malloc(sz);
            memcpy(cpus, tmp_arr, sz);

            dst->dom_entries[e].cpus = cpus;
            dst->dom_entries[e].num_of_cpus = (int32_t) nr_set_bits;
        }
        else {
            // dst->dom_entries[e].cpus = NULL;
            dst->dom_entries[e].num_of_cpus = 0;
        }

        dst->dom_entries[e].domid = src->dom_entries[e].domid;

        dst->dom_entries[e].gang_sched_policy =
            src->dom_entries[e].gang_sched_policy;
    }

    free(tmp_arr);
    return rc;
}


int xc_sched_gang_params_set(xc_interface* xch, 
                             uint32_t cpupool_id,
                             gang_sched_params_t* params) {

    int rc = 0;
    xen_sysctl_gang_schedule_t __sysctl_params = { 0 };
    xen_sysctl_gang_schedule_t* sysctl_params = &__sysctl_params;

    DECLARE_SYSCTL;

    DECLARE_HYPERCALL_BOUNCE(sysctl_params,
                             sizeof(xen_sysctl_gang_schedule_t),
                             XC_HYPERCALL_BUFFER_BOUNCE_IN);

    xc_hypercall_buffer_array_t* hc_buf_arr = NULL; 

    hc_buf_arr = xc_hypercall_buffer_array_create(xch, params->num_dom_entries);
    if (hc_buf_arr == NULL) {
        PERROR("xc_hypercall_buffer_array_create(...) failed! Error code: %d \n", rc);
        return -ENOMEM;
    }


    rc = copy_params_into_sysctl(xch, params, sysctl_params, hc_buf_arr);
    if (rc) {
        PERROR("copy_params_into_sysctl(...) failed! Error code: %d \n", rc);
        goto OUT;
    }

    rc = xc_hypercall_bounce_pre(xch, sysctl_params);
    if (rc) {
        PERROR("xc_hypercall_bounce_pre(...) failed! Error code: %d \n", rc);
        goto OUT;
    }

    sysctl.cmd = XEN_SYSCTL_scheduler_op;
    sysctl.u.scheduler_op.cpupool_id = cpupool_id;
    sysctl.u.scheduler_op.sched_id = XEN_SCHEDULER_GANG;
    sysctl.u.scheduler_op.cmd = XEN_SYSCTL_SCHEDOP_putinfo;
    set_xen_guest_handle(sysctl.u.scheduler_op.u.sched_gang.params,
                         sysctl_params);

    rc = do_sysctl(xch, &sysctl);

    xc_hypercall_bounce_post(xch, sysctl_params);


OUT:
    xc_hypercall_buffer_array_destroy(xch, hc_buf_arr);

    return rc;

}


int xc_sched_gang_params_get(xc_interface* xch,
                             uint32_t cpupool_id,
                             gang_sched_params_t* params) {
    int rc = 0;
    xen_sysctl_gang_schedule_t __sysctl_params = { 0 };
    xen_sysctl_gang_schedule_t* sysctl_params = &__sysctl_params;

    DECLARE_SYSCTL;
    DECLARE_HYPERCALL_BOUNCE(sysctl_params,
                             sizeof(xen_sysctl_gang_schedule_t),
                             XC_HYPERCALL_BUFFER_BOUNCE_BOTH);

    ///////////////////////////////////////////////////////////////////////////
    // Prepare sysctl_params to receive paramaters.
    // In particular, we allocate buffers for the CPU bitmaps.
    ///////////////////////////////////////////////////////////////////////////

    xc_hypercall_buffer_array_t* hc_buf_arr = NULL;

    hc_buf_arr = xc_hypercall_buffer_array_create(xch, GANG_SCHED_MAX_DOMAINS);
    if (hc_buf_arr == NULL) {
        PERROR("xc_hypercall_buffer_array_create(...) failed! Error code: %d \n", rc);
        return -ENOMEM;
    }

    rc = reset_sysctl_and_alloc_bitmaps(xch, sysctl_params, hc_buf_arr);
    if (rc) {
        PERROR("reset_sysctl_and_alloc_bitmaps(...) failed! Error code: %d \n", rc);
        goto OUT;
    }

    ///////////////////////////////////////////////////////////////////////////

    rc = xc_hypercall_bounce_pre(xch, sysctl_params);
    if (rc) {
        PERROR("xc_hypercall_bounce_pre(...) failed! Error code: %d \n", rc);
        goto OUT;
    }

    sysctl.cmd = XEN_SYSCTL_scheduler_op;
    sysctl.u.scheduler_op.cpupool_id = cpupool_id;
    sysctl.u.scheduler_op.sched_id = XEN_SCHEDULER_GANG;
    sysctl.u.scheduler_op.cmd = XEN_SYSCTL_SCHEDOP_getinfo;
    set_xen_guest_handle(sysctl.u.scheduler_op.u.sched_gang.params,
                         sysctl_params);

    rc = do_sysctl(xch, &sysctl);

    if (rc) {
        PERROR("do_sysctl(...) failed! Error code: %d \n", rc);
        goto OUT;
    }

    get_xen_guest_handle(sysctl_params,
                         sysctl.u.scheduler_op.u.sched_gang.params);

    rc = copy_sysctl_into_params(xch, sysctl_params, params);
    if (rc) {
        PERROR("copy_sysctl_into_params(...) failed! Error code: %d \n", rc);
    }

    xc_hypercall_bounce_post(xch, sysctl_params);

OUT:
    xc_hypercall_buffer_array_destroy(xch, hc_buf_arr);

    return rc;
}


