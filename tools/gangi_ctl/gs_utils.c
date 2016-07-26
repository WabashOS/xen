/*
 * gs_utils.c
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
#include <errno.h>
#include <assert.h>

#include <xenctrl.h>

#include "gs_utils.h"


int validate_cpupool(xc_interface* xch, uint32_t cpupool_id) {

    int rc = 0;
    xc_cpupoolinfo_t* cpupool_info = NULL;  
    int sz;
 
    assert(xch != NULL);

    cpupool_info = xc_cpupool_getinfo(xch, cpupool_id);
    if (cpupool_info == NULL) {
        fprintf(stderr, "Coudn't get info for cpupool ID %d\n", cpupool_id);
        return -EINVAL; 
    }

    if (cpupool_id != cpupool_info->cpupool_id) {
        fprintf(stderr, 
                "Invalid cpupool ID. Passed ID: %d, retrieved ID: %d \n", 
                cpupool_id, cpupool_info->cpupool_id);
        rc = -EINVAL; 
        goto OUT;
    }


    if (cpupool_info->sched_id != XEN_SCHEDULER_GANG) {
        fprintf(stderr, 
                "Invalid cpupool ID %d. Not running the gang scheduler\n", 
                cpupool_id);
        rc = -EINVAL; 
        goto OUT;
    }


    sz = xc_get_cpumap_size(xch);
    assert(sz > 0);

    rc = -EINVAL; 
    for (int i = 0; i < sz; i++) {
        if (cpupool_info->cpumap[i]) {
            rc = 0; 
            break;
        }
    }

OUT:
    xc_cpupool_infofree(xch, cpupool_info);
    return rc;
}


int num_of_domains_in_cpupool(xc_interface* xch, uint32_t cpupool_id) {

    uint32_t n_dom = 0;

    xc_cpupoolinfo_t* cpupool_info = xc_cpupool_getinfo(xch, cpupool_id);
    if (cpupool_info == NULL) {
        fprintf(stderr, "Coudn't get info for cpupool ID %d\n", cpupool_id);
        return -EINVAL; 
    }

    n_dom = cpupool_info->n_dom;

    xc_cpupool_infofree(xch, cpupool_info);

   return (int) n_dom;
}



int validate_domain_id(xc_interface* xch, 
                       uint32_t cpupool_id,
                       uint32_t dom_id) {
    int rc;
    xc_domaininfo_t dom_info;

    assert(xch != NULL);

    rc = xc_domain_getinfolist(xch, dom_id, 1, &dom_info);

    if (rc < 0) {
        fprintf(stderr, "Coudn't get info for domain ID %d\n", dom_id);
        return -EINVAL;
    }

    if (rc != 1) {
        fprintf(stderr, "Got more than one info for domain ID %d\n", dom_id);
        return -EINVAL;
    }

    if (dom_info.domain != dom_id) {
        fprintf(stderr, 
                "Domain info for domain ID %d with different domain ID\n", 
                dom_id);
        return -EINVAL;
    }

    if (dom_info.cpupool != cpupool_id) {
        fprintf(stderr, 
                "Domain with ID %d is not in CPU pool with ID %d\n", 
                dom_id, cpupool_id);
        return -EINVAL;
    }

    return 0;
} 


int validate_cpu_array(xc_interface* xch, 
                       uint32_t cpupool_id,
                       int32_t* cpus,
                       int32_t num_of_cpus) {
    int max_cpus;

    xc_cpupoolinfo_t* info = NULL;
    uint8_t present;

    assert(xch != NULL);

    if (cpus == NULL) {
        fprintf(stderr, "Array of CPUs is null.\n");
        return -EINVAL;
    }

    if (num_of_cpus <= 0) {
        fprintf(stderr, "Invalid number of CPUs in the array. "
                "Value = %d\n", num_of_cpus);
        return -EINVAL;
    }

    max_cpus = xc_get_max_cpus(xch);

    if (num_of_cpus > max_cpus) {
        fprintf(stderr, "Invalid number of CPUs in the array. "
                "Value = %d > Max. CPUs = %d \n", num_of_cpus, max_cpus);
        return -EINVAL;
    }


    for (int i = 0; i < num_of_cpus; i++) {
        if (cpus[i] < 0 || cpus[i] >= max_cpus) {
            fprintf(stderr, "Invalid CPU ID (%d) in element %d of CPU array.\n", 
                    cpus[i], i);
            return -EINVAL;
        }        
    }

    // Cannot be repeated elements.
    for (int i = 0; i < (num_of_cpus - 1); i++) {
        for (int j = (i + 1); j < num_of_cpus; j++) {
            if (cpus[i] == cpus[j]) {
                fprintf(stderr, "Repeated elements (%d, %d) in CPU array. "
                        "Repeated value = %d.\n", 
                        i, j, cpus[i]);
                return -EINVAL;
            }
        }        
    }


    // All CPUs in the array must belong to the CPU pool.
    info = xc_cpupool_getinfo(xch, cpupool_id);
    if (info == NULL) {
        fprintf(stderr, "Couldn't get information about CPU pool ID %d.\n", cpupool_id);
        return -EINVAL;
    }

    // cpumap_size = xc_get_cpumap_size(xch);
    present = 1;
    for (int i = 0; i < num_of_cpus; i++) {

        int w = cpus[i] / sizeof(xc_cpumap_t);
        int bit = cpus[i] % sizeof(xc_cpumap_t); 
        
        present = (info->cpumap[w]) & (1 << bit);    
    
        if (!present) {
            fprintf(stderr,
                    "Invalid CPU ID (%d) in element %d of CPU array. "
                    "It is not assigned to CPU pool ID %d\n", 
                    cpus[i], i, cpupool_id);
            break;
        }

    }

    if (info != NULL) {
        xc_cpupool_infofree(xch, info);
    }
 
    if (!present) {
        return -EINVAL;
    }        

    return 0;
}


int validate_params(xc_interface* xch, 
                    uint32_t cpupool_id,
                    gang_sched_params_t* params) {

    int rc = 0;
    gang_sched_policy_type_t pol_type; 


    assert(xch != NULL);
    assert(params != NULL);

    if (params->num_dom_entries == 0) {
        fprintf(stderr, "Invalid params. " 
                "Specified number of domain entries is zero.\n"); 
        return -EINVAL;
    }

    if (params->num_dom_entries > GANG_SCHED_MAX_DOMAINS) {
        fprintf(stderr, "Invalid params. " 
                "Specified number of domain entries (%d) > %d.\n", 
                params->num_dom_entries, GANG_SCHED_MAX_DOMAINS);
        return -EINVAL;
    }


    for (uint16_t i = 0; i < params->num_dom_entries; i++) {
        int32_t dom_id = params->dom_entries[i].domid;
        if (dom_id < 0) {
            fprintf(stderr, "Invalid domain ID (%d) in entry (%d)\n", 
                    dom_id, i);
            return -EINVAL;
        }

        rc = validate_domain_id(xch, cpupool_id, (uint32_t) dom_id);
        if (rc < 0) {
            fprintf(stderr, "Invalid domain ID (%d) in entry (%d)\n", 
                    dom_id, i);
            return rc;
        }


        rc = validate_cpu_array(xch, 
                                cpupool_id, 
                                params->dom_entries[i].cpus,
                                params->dom_entries[i].num_of_cpus);
        if (rc < 0) {
            fprintf(stderr, "Invalid CPU array in entry (%d)\n", i);
            return rc;
        }


        pol_type = params->dom_entries[i].gang_sched_policy.type;
        if (!VALIDATE_GANG_SCHED_POLICY_TYPE(pol_type)) {
            fprintf(stderr, 
                    "Invalid type of time-multiplexing policy (%s) in entry (%d).\n", 
                    GANG_SCHED_POLICY_2_STR(pol_type), i);
            return -EINVAL;
        } 
    }

    return 0;
};



void print_gang_sched_params(gang_sched_params_t* params) {

    printf("Number of domains = %d\n", params->num_dom_entries);

    for (int32_t e = 0; e < params->num_dom_entries; e++) {
        int32_t num_of_cpus;

        printf("Domain ID = %d\n", params->dom_entries[e].domid);

        num_of_cpus = params->dom_entries[e].num_of_cpus;
        printf("Assigned CPUs (%d) = [", num_of_cpus);
        for(int32_t c = 0; c < num_of_cpus; c++) {
            if (c < (num_of_cpus - 1)) {
                printf("%d, ", params->dom_entries[e].cpus[c]);
            }
            else {
                printf("%d]\n", params->dom_entries[e].cpus[c]);
            }
        }

        print_gang_sched_policy(&params->dom_entries[e].gang_sched_policy);
    }
}


void print_gang_sched_policy(gang_sched_policy_t* p) {

    printf("Gang scheduling policy = %s (%d)\n",
           GANG_SCHED_POLICY_2_STR(p->type), p->type);

    if (p->type == GANG_NO_MUXING) {
        printf("from = %lu ms\n",
               (p->params.no_muxing_params.from / 1000000UL));
    }
    else if (p->type == GANG_TIME_TRIG_MUXING) {
        printf("from = %lu ms, period = %lu ms, active time = %lu ms, "
               "space filling = %s\n",
               (p->params.tt_muxing_params.from / 1000000UL),
               (p->params.tt_muxing_params.period / 1000000UL),
               (p->params.tt_muxing_params.active_time / 1000000UL),
               (p->params.tt_muxing_params.space_filling) ? "TRUE" : "FALSE");
    }
    else if (p->type == GANG_EVENT_TRIG_MUXING) {
        printf("from = %lu ms, period = %lu ms, active time = %lu ms, "
               "space filling = %s\n",
               (p->params.et_muxing_params.from / 1000000UL),
               (p->params.et_muxing_params.period / 1000000UL),
               (p->params.et_muxing_params.active_time / 1000000UL),
               (p->params.et_muxing_params.space_filling) ? "TRUE" : "FALSE");
    }
    else if (p->type == GANG_BEST_EFFORT_MUXING) {
        printf("from = %lu ms, weight = %d, space filling = %s\n",
               (p->params.be_muxing_params.from / 1000000UL),
               (p->params.be_muxing_params.weight),
               (p->params.be_muxing_params.space_filling) ? "TRUE" : "FALSE");
    }
}

