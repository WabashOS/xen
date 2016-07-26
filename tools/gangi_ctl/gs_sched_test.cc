/*
 * gs_sched_test.c
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

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <bitmask.h>

#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <iostream>

#include "gs_sched_test.h"

/** Validates time multiplexing policy. */
static 
bool __validate_muxing_policy(gang_sched_policy_t& p) {

    if (!VALIDATE_GANG_SCHED_POLICY_TYPE(p.type)) {
        return false;
    } 

    if (p.type == GANG_TIME_TRIG_MUXING) {
        if (p.params.tt_muxing_params.from == INFINITY) {
            return false;
        }

        if (p.params.tt_muxing_params.active_time >=
            p.params.tt_muxing_params.period) {
            return false;
        }
    }
    else if (p.type == GANG_EVENT_TRIG_MUXING) {
        if (p.params.et_muxing_params.from == INFINITY) {
            return false;
        }

        if (p.params.et_muxing_params.active_time >=
            p.params.et_muxing_params.period) {
            return false;
        }
    }
    else if (p.type == GANG_BEST_EFFORT_MUXING) {
        if (p.params.be_muxing_params.from == INFINITY) {
            return false;
        }

        if (p.params.be_muxing_params.weight == 0) {
            return false;
        }
    }
    else { // (pol_type == GANG_NO_MUXING) 
        if (p.params.no_muxing_params.from == INFINITY) {
            return false;
        }
    }

    return true;
}


/** Validates domain infos. */
static 
bool __validate_dominfos(gs_dominfo_t** arr, size_t len, size_t cpu_count) {

    for (size_t d = 0; d < len; d++) {
        gs_dominfo_t* di = arr[d];

        if (di == NULL) {
            printf("domain %d is nil\n", di->domid);
            return false;
        }

        // Validate domain identifiers
        if (di->domid < 0) {
            printf("domain %d with invalid (negative) identifier %d\n", 
                    di->domid, di->domid);
            return false;
        }

        // Validate number of CPUs
        if (di->num_of_cpus < 0 || ((int32_t)cpu_count) < di->num_of_cpus) {
            printf("domain %d with invalid number of CPUs %d\n", 
                    di->domid, di->num_of_cpus);
            return false;
        }

        // Validate the CPU identifiers.
        for (int32_t c = 0; c < di->num_of_cpus; c++) {
            int32_t cpuid = di->cpus[c];
            assert(cpuid >= 0);
            assert(cpuid < ((int32_t)cpu_count));
            if (cpuid < 0 || ((int32_t)cpu_count) <= cpuid) {
                printf("domain %d with invalid CPU[%d] =  %d\n", 
                        di->domid, c, cpuid);
                return false;
            }
        }

        // Validate time multiplexing policy.
        if (!__validate_muxing_policy(di->gang_sched_policy)) {
            printf("Invalid time-multiplexing policy (%s) in entry (%lu) "
                    "[domid = %d].\n", 
                    GANG_SCHED_POLICY_2_STR(di->gang_sched_policy.type), 
                    d, 
                    di->domid);
            return false;
        }

    }

    return true;
}

/** Datatype for map between a CPU to the domains running on the CPU. */
typedef std::map<size_t, std::set<size_t> > cpu2dom_map_t;

/** Populates the map between a CPU to the domains running on the CPU. */
static 
void __populate_cpu2dom(gs_dominfo_t** arr, 
                        size_t len, 
                        size_t cpu_count,
                        cpu2dom_map_t& cpu2dom) {
    assert(cpu2dom.empty());

    std::set<size_t> emptySet;
    for (size_t c = 0; c < cpu_count; c++) {
        cpu2dom[c] = emptySet;
    }

    for (size_t d = 0; d < len; d++) {
        gs_dominfo_t* di = arr[d];
       
        assert(di->num_of_cpus > 0);
        assert(di->num_of_cpus <= (int32_t) cpu_count);

        for (int32_t c = 0; c < di->num_of_cpus; c++) {
            int32_t cpuid = di->cpus[c];
            assert(cpuid >= 0);
            assert(cpuid < ((int32_t)cpu_count));
            
            auto ret = cpu2dom[size_t(cpuid)].insert(size_t(di->domid));
            assert(ret.second == true); 
        }
    }
}



int 
are_schedulable(gs_dominfo_t** arr, 
                size_t len, 
                size_t be_reserve, 
                uint64_t be_basic_period, 
                size_t cpu_count) {


    // Verify parameters. 

    assert(arr != NULL);

    if (cpu_count <= 0) {
        return -EINVAL; 
    }

    if (be_reserve == 100) {
        return 1;
    }
    else if (be_reserve > 100) {
        return -EINVAL; 
    }

    if (be_reserve > 50) {
        printf("WARNING: CPU reservation for best-effort domains is %lu %%; " 
               "it seems too high.\n", be_reserve);
    }

    if (be_basic_period == 0) {
        return -EINVAL; 
    }

    if (!__validate_dominfos(arr, len, cpu_count)) {
        return -EINVAL;
    }

    // The 'cpu2dom' map is just instrumental in creating the report in case the
    // test fails. 
    cpu2dom_map_t cpu2dom;
    __populate_cpu2dom(arr, len, cpu_count, cpu2dom);

   
   //////////////////////////////////////////////////////////////////////////////// 
   // Actual test code
   //////////////////////////////////////////////////////////////////////////////// 

    double bedom_util = ((double)be_reserve) / 100; 
    // std::cout << "bedom_util = " << bedom_util << std::endl;

    std::vector<double> total_util_per_cpu(cpu_count, 0.0);
    std::vector<bool> bedom_considered_in_cpu(cpu_count, false);

    for (size_t d = 0; d < len; d++) {
        gs_dominfo_t* di = arr[d];
       
        assert(di->num_of_cpus > 0);
        assert(di->num_of_cpus <= (int32_t) cpu_count);
    
        gang_sched_policy_t& p = di->gang_sched_policy;

        double u = 0.0;

        if (p.type == GANG_NO_MUXING) {
            u = 1.0;
        }
        else if (p.type == GANG_TIME_TRIG_MUXING) {
            u = ((double) p.params.tt_muxing_params.active_time) / p.params.tt_muxing_params.period;
        }
        else if (p.type == GANG_EVENT_TRIG_MUXING) {
            u = ((double) p.params.et_muxing_params.active_time) / p.params.et_muxing_params.period;
        }
        else if (p.type == GANG_BEST_EFFORT_MUXING) {
            u = bedom_util;
        }
        else {
            fprintf(stderr, "Invalid time multiplexing policy.\n");
            exit(EXIT_FAILURE);
        }

        assert(u > 0.0);
        assert(u <= 1.0);

        for (int32_t c = 0; c < di->num_of_cpus; c++) {
            size_t cpuid = di->cpus[c];

            if (p.type == GANG_BEST_EFFORT_MUXING) {
                if (bedom_considered_in_cpu[cpuid] == false) {
                    total_util_per_cpu[cpuid] += u;
                    bedom_considered_in_cpu[cpuid] = true;
                }
            }
            else {
                total_util_per_cpu[cpuid] += u;
            }

            // NOTE:
            // We could check if total_util_per_cpu[cpuid] > 1.0, and terminate
            // early returning that the test failed.
            // But I rather give the user a more informative output when the
            // test fails, which includes the CPUs and domains that made the
            // test fail.
        }
    }


    std::stringstream ss;

    int rval = 0;
    for (size_t cpuid = 0; cpuid < cpu_count; cpuid++) {
        //std::cout << "total_util_per_cpu[" << cpuid << "] = " 
        //          << total_util_per_cpu[cpuid] << std::endl;

        if (total_util_per_cpu[cpuid] > 1.0) {
            rval = 1;

            ss << "   Test failed on CPU " << cpuid << " with domains: ";

            std::set<size_t>& doms_on_cpu = cpu2dom[cpuid];
            std::set<size_t>::iterator dit;
            for (dit = doms_on_cpu.begin(); dit != doms_on_cpu.end(); dit++) {
                ss << *dit << ' ';
            }
            ss << std::endl;
        }
    }

    
    std::cout << "Schedulability Test Report: " 
              << ((rval == 0) ? "PASSED":"FAILED") 
              << std::endl;
    if (rval != 0) {
        std::cout << ss.str() << std::endl;
    }

    return rval;
}
 

