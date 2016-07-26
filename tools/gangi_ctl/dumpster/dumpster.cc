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


/** Number of CPUs in the system. */
static size_t num_of_cpus = 0; 


//////////////////////////////////////////////////////////////////////////// 
// CPU COHORTS
//
// A 'domain cohort' is a group of domains in which a domain overlaps with at
// least another domain on one or more CPUs.
// A 'CPU cohort' includes the CPUs allocated to the domains that form a domain
// cohort.
//
// The notion of cohort is important because, for correct gang scheduling, the
// local schedulers residing on the CPUs of a cohort need to maintain an
// identical view of the state of that portion of the system.
//
// Here CPU and hardware thread are treated as the synonyms.
//////////////////////////////////////////////////////////////////////////// 

/** 
 * Array that indicates the membership of each CPU to a cohort. 
 * That is, the i-th element in the array stores the ID of the cohort the
 * i-th CPU in the system belongs to.  If the i-th element is
 * negative, then the i-th CPU belongs to no cohort
 *
 * Valid cohort indexes can go from 0 to (num_of_cpus - 1).
 *
 * The size of the array is the number of CPUs in the systems.
 */ 
static 
int* cpu_2_cohort = NULL; 

/** 
 * Array of bitmasks indicating which CPUs belong to each cohort. 
 * The size of the array is the number of CPUs in the system.
 * Note that (the number of cohorts) <= (the number of CPUs).
 */
static 
struct bitmask** cohorts = NULL;

/**
 * Array that indicates the number of best-effort domains in each cohort.
 * The size of the array is the number of CPUs in the systems.
 */
static 
int* be_doms_in_cohort = NULL; 
 
/** 
 * Number of existing cohorts.
 * It can go from 0 to the number of CPUs in the system. 
 */
static size_t num_of_cohorts = 0; 





/** Allocates and initializes the cohort arrays. */
SUPPRESS_NOT_USED_WARN
static 
void __create_cohort_arrays(size_t cpu_count) {

    assert(cpu_2_cohort == NULL);
    assert(cohorts == NULL);
    assert(be_doms_in_cohort == NULL);
    assert(num_of_cohorts == 0);

    cpu_2_cohort = (int*) calloc(cpu_count, sizeof(int));
    for (size_t i = 0; i < cpu_count; i++) {
        cpu_2_cohort[i] = -1;
    }

    cohorts = (struct bitmask**) calloc(cpu_count, sizeof(struct bitmask*));
    for (size_t i = 0; i < cpu_count; i++) {
        struct bitmask* bm = bitmask_alloc(cpu_count);
        assert(bm != NULL); // bitmask_alloc failed.
        bitmask_clearall(bm);

        cohorts[i] = bm;
    }

    be_doms_in_cohort = (int*) calloc(cpu_count, sizeof(int));

}


/** Frees the cohort arrays. */
SUPPRESS_NOT_USED_WARN
static 
void __destroy_cohort_arrays(void) {

    assert(cpu_2_cohort != NULL);
    assert(cohorts != NULL);
    assert(be_doms_in_cohort != NULL);

    free(cpu_2_cohort);

    size_t len = sizeof(cohorts) / sizeof(cohorts[0]);
    for (size_t i = 0; i < len; i++) {
        bitmask_free(cohorts[i]);
    }
    free(cohorts);
    
    free(be_doms_in_cohort);
}


/**
 * Identifies and merges cohorts as necessary.
 *
 * @param[in] di  
 * @param[in,out] __cohorts
 * @param[in,out] num_of_cohorts
 */
static void 
__update_cohorts(gs_dominfo_t* di, 
                 struct bitmask** kohorts, 
                 size_t* num_of_kohorts) {

    // TODO: Implement


//    // TODO: Revise this function. Can we make it more efficient?
//    // TODO: Add log messages to ease degugging.
//
//    cpumask_t* the_cohort;
//    cpumask_t* new_cohort;
//    int cohort_id;
//    int i;
//
//    bool_t intersect = FALSE;

    assert(di != NULL);

//    // Does the domain's CPU mask intersect any existing cohort?  
//   
//    for (size_t n = 0; n < (*__num_of_cohorts); n++) {
//        the_cohort = &__cohorts[n];
//        if ((intersect = cpumask_intersects(&di->cpumask, the_cohort))) {
//            // The domain's CPU mask and the cohort's CPU mask have some common bits.
//
//            if (!cpumask_subset(&di->cpumask, the_cohort)) {
//                // But the domain's CPU mask is NOT a subset of the cohort's CPU
//                // mask. That is, there are some bits that are NOT common.
//                // Then, expand the cohort's reach.  
//
//                cpumask_or(the_cohort, the_cohort, &di->cpumask);
//            }
//
//            break;
//        }
//    }   
//
//    if (!intersect) {
//        // No intersection between the the domain's CPU mask and any of the
//        // cohorts.  Then, create a new cohort with the CPUs required by the
//        // domain.
//        
//        cohort_id = (*__num_of_cohorts); // Get a new/unused cohort 
//        (*__num_of_cohorts)++;
//
//        new_cohort = &__cohorts[cohort_id];
//        cpumask_or(new_cohort, new_cohort, &di->cpumask);
//    }   
//
//    // There may be overlapping cohorts at this point, if so we must merge them.
//    i = 0;
//    while(i < ((*__num_of_cohorts)-1)) {
//
//        for (int j = i+1; j < (*__num_of_cohorts); j++) {
//            cpumask_t* ca = &__cohorts[i];
//            cpumask_t* cb = &__cohorts[j];
//            if (cpumask_intersects(ca, cb) ) {
//                // Cohorts A and B overlap, then merge them.
//                cpumask_or(ca, ca, cb);
//
//                // Fill the hole in the array of cohorts.
//                for (int k = j+1; k < (*__num_of_cohorts); k++) {
//                    cpumask_t* cx = &__cohorts[k-1];
//                    cpumask_t* cy = &__cohorts[k];
//                    cpumask_copy(cx, cy);     
//                }   
//
//                (*__num_of_cohorts)--;
//                
//                i = -1; // Restart the outer 'while' loop.
//                break;
//            }   
//        }   
//
//        i++;
//    }   
}




///**
// * Selects and sets the CPU cohort that corresponds to the given domain.
// * 
// * @param di domain info.
// * @param cohorts array of CPU cohorts, with each element being a CPU mask.
// * @param num_of_cohorts number of CPU cohorts.
// */
//SUPPRESS_NOT_USED_WARN
//static void 
//__set_cohort_in_domain(gang_dom_info_t* di, 
//                       cpumask_t* cohorts, 
//                       size_t num_of_cohorts) {
//
//    // TODO: Add log messages to ease degugging.
//
//    bool_t found = FALSE;
//
//    for (size_t nc = 0; nc < num_of_cohorts; nc++) {
//        cpumask_t* the_cohort = &cohorts[nc];
//        if (cpumask_subset(&di->cpumask, the_cohort)) {
//            // The domain belongs to the cohort because the domain's CPU mask is
//            // a subset of the cohort's CPU.
//            di->cohort = nc;
//            found = TRUE;
//            break;
//        }
//    }
//
//    BUG_ON(found == FALSE);
//}


/**
 * Identifies the cohorts and populates the passed cohort arrays accordingly.
 *
 * @param[in] arr Array of domain infos describing domain allocations.
 * @param[in] len Length of 'arr'.
 * @param[out] cpu_2_kohort 
 * @param[out] kohorts
 * @param[out] be_doms_in_kohort
 * @param[out] num_of_kohorts
 */
SUPPRESS_NOT_USED_WARN
static void 
__populate_cohorts(gs_dominfo_t** arr, 
                   size_t len, 
                   int* cpu_2_kohort, 
                   struct bitmask** kohorts,
                   int* be_doms_in_kohort,
                   size_t* num_of_kohorts) {
    
    // Sanity checks on cohort variables.
    assert(*num_of_kohorts == 0);
    for (size_t j = 0; j < num_of_cpus; j++) {
        assert(cpu_2_kohort[j] == -1);
        assert(bitmask_isallclear(kohorts[j]));
        assert(be_doms_in_kohort[j] == 0);
    }

    if (len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        __update_cohorts(arr[i], kohorts, num_of_kohorts);
    }

    assert(*num_of_kohorts > 0);
    assert(*num_of_kohorts > num_of_cpus);


    // TODO: I am here.

/*                                 

    // Set the 'cpu_2_cohort' map
    for (size_t c = 0; c < (*__num_of_cohorts); c++) {
        cpumask_t* the_cohort = &__cohorts[c];
        int cpu_id;
        for_each_cpu(cpu_id, the_cohort) {
            __cpu_2_cohort[cpu_id] = c;
        }
    } 

    for (uint16_t i = 0; i < len; i++) {
        gang_dom_info_t* di = arr[i];
        __set_cohort_in_domain(di, __cohorts, (*__num_of_cohorts));
    }

    // Counting best-effort domains in cohorts.
    for (uint16_t i = 0; i < len; i++) {
        gang_dom_info_t* di = arr[i];
        if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
            __be_doms_in_cohort[di->cohort]++;
        }
    }


   /////////////////////////////////////////////////////////////////////////////
   // For debugging
   /////////////////////////////////////////////////////////////////////////////
    GANG_LOGT("num_of_cohorts = %lu, num_of_muxgroups = %lu\n", 
              *__num_of_cohorts, *__num_of_muxgroups);

    GANG_LOGT("cpu_to_cohort:\n");
    for (int j = 0; j < NR_CPUS; j++) {
        printk("%d,",  __cpu_2_cohort[j]);
    }
    printk("\n");

    GANG_LOGT("cpu_to_muxgroup:\n");
    for (int j = 0; j < NR_CPUS; j++) {
        printk("%d,",  __cpu_2_muxgroup[j]);
    }
    printk("\n");

    GANG_LOGT("Best-effort domains per cohort:\n");
    for (int j = 0; j < NR_CPUS; j++) {
        printk("%d,",  __be_doms_in_cohort[j]);
    }
    printk("\n");


    GANG_LOGT("Per domain cohort and muxgroups:\n");
    for (uint16_t i = 0; i < len; i++) {
        gang_dom_info_t* di = arr[i];
        char cpustr[(2*NR_CPUS)];
        char mgstr[(2*NR_CPUS)];

        memset(cpustr, 0, sizeof(cpustr));
        cpumask_scnprintf(cpustr, sizeof(cpustr), &di->cpumask);

        memset(mgstr, 0, sizeof(mgstr));
        cpumask_scnprintf(mgstr, sizeof(mgstr), &di->muxgroup_mask);

        printk("    Domain: %d, cpumask: %s \n" 
               "        cohort: %d, muxgroup mask: %s \n", 
               __get_domid_from_dom_info(di), cpustr, di->cohort, mgstr);
    }
    printk("\n");

   /////////////////////////////////////////////////////////////////////////////
*/
}


/** Compares two domain info items based on their time multiplexing policies. */
static 
int __compare_dominfos(const void* a, const void* b) {
    gs_dominfo_t* di0 = (gs_dominfo_t*) a;
    gs_dominfo_t* di1 = (gs_dominfo_t*) b;
    return (di0->gang_sched_policy.type - di1->gang_sched_policy.type);
}

/**
 * Sorts an array of domain info items according the expected order by the
 * schedulability test. 
 */
SUPPRESS_NOT_USED_WARN
static 
void __sort_dominfos(gs_dominfo_t** arr, size_t len) {
    qsort(arr, len, sizeof(gs_dominfo_t*), __compare_dominfos);
}


////////////////////////////////////////////////////////////////////////////////
// C++ typedefs
////////////////////////////////////////////////////////////////////////////////

/** Datatype of map between a domain ID to its domain info. */
typedef std::map<size_t, GsDomainInfo*> did2dinfo_map_t;

/** Datatype for map between a CPU to the domains running on the CPU. */
typedef std::map<size_t, std::set<size_t> > cpu2dom_map_t;

/** Datatype for domain to overlapping domains map. */
typedef std::map<size_t, std::set<size_t> > dom2overlap_map_t;

////////////////////////////////////////////////////////////////////////////////


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


static 
void __populate_did2dinfo(gs_dominfo_t** arr, 
                          size_t len, 
                          did2dinfo_map_t& did2dinfo) {
    assert(did2dinfo.empty());

    for (size_t d = 0; d < len; d++) {
        std::pair<size_t,GsDomainInfo*> kvpair(arr[d]->domid, arr[d]);
        auto ret = did2dinfo.insert(kvpair); 
        assert(ret.second == true);
    }
}


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


static 
void __populate_dom2overlap(gs_dominfo_t** arr, 
                            size_t len, 
                            size_t cpu_count,
                            dom2overlap_map_t& dom2overlap) {
    assert(dom2overlap.empty());

    struct bitmask* cpumask_per_dom[len];
    for (size_t d = 0; d < len; d++) {
        struct bitmask* bm = bitmask_alloc(cpu_count);
        assert(bm != NULL); // bitmask_alloc failed.
        bitmask_clearall(bm);

        gs_dominfo_t* di = arr[d];
        for (int32_t c = 0; c < di->num_of_cpus; c++) {
            size_t cpuid = (size_t) di->cpus[c];
            bitmask_setbit(bm, cpuid);
        }
        cpumask_per_dom[d] = bm;
    }

    for (size_t i = 0; i < len; i++) {
        for (size_t j = 0; j < len; j++) {
            if (i != j) {
                int intersec = bitmask_intersects(cpumask_per_dom[i],
                                                  cpumask_per_dom[j]);
                if (intersec) {
                   dom2overlap[i].insert(j); 
                }
            }
        }
    }

    for (size_t d = 0; d < len; d++) {
        bitmask_free(cpumask_per_dom[d]);
    }
}

/** Calculates utilization. */
inline static double 
__get_util(double active_time, double period) {
    return (active_time / period);
}


/**
 * Calculates the Demand-Bound Function (DBF*).
 *
 * For details refer to the paper "A Comparison of Global and Partitioned EDF
 * Schedulability Tests for Multiprocessors" by Theodore P. Baker, page 2.
 *
 * @param active_time
 * @param period 
 * @param interval the time frame to be considered to calculate DBF*.
 * @returns the value of DBF*. 
 */
static double 
__calc_dbf(double active_time, double period, double interval) {
    double dbf = 0.0;
    if (period < interval) {
        double util = __get_util(active_time, period);
        assert(util > 0.0 && util <= 1.0);
        dbf = active_time + (interval - period) * util;
    }
    return dbf; 
}


/** Test schedulability of a domain. */
static 
bool __test_dom(size_t domid, 
                std::set<size_t>& doms_on_cpu, 
                std::set<size_t>& overlappers, 
                size_t be_reserve, 
                uint64_t be_basic_period,
                did2dinfo_map_t& did2dinfo) {

    // Sanity check: 'domid' is in 'doms_on_cpus map'
    assert(doms_on_cpu.find(domid) != doms_on_cpu.end());

    // Sanity check: 'domid' is in 'doms_on_cpus map'
    assert(overlappers.find(domid) == overlappers.end());


    GsDomainInfo* di = did2dinfo[domid]; 
    gang_sched_policy_t& p = di->gang_sched_policy;

    uint64_t period;
    uint64_t active_time;

    if (p.type == GANG_NO_MUXING) {
        return (doms_on_cpu.size() == 1);
    }
    else if (p.type == GANG_BEST_EFFORT_MUXING) {
        return (be_reserve > 0.0);
    }
    else if (p.type == GANG_TIME_TRIG_MUXING) {
        period = p.params.tt_muxing_params.period;
        active_time = p.params.tt_muxing_params.active_time;
    }
    else if (p.type == GANG_EVENT_TRIG_MUXING) {
        period = p.params.et_muxing_params.period;
        active_time = p.params.et_muxing_params.active_time;
    }
    else {
        fprintf(stderr, "Invalid time multiplexing policy.\n");
        exit(EXIT_FAILURE);
    }

    double u = __get_util(active_time, period);
    assert(u > 0.0);
    assert(u <= 1.0);


    // Collective quantum (active time) per basic period for BE domains.
    double be_quantum = (be_basic_period * be_reserve) / 100;


    // Calculate the Demand-Bound Function (DBF) and utilization for the rest of
    // the cells.
    double total_util = 0.0;
    double total_dbf = 0.0;


    bool be_doms_present = false;            

    // The domains to consider in DBF calculation.
    std::set<size_t> other_doms; 
    
    for (auto it = doms_on_cpu.begin(); it != doms_on_cpu.end(); it++) {
        if (*it != domid) {
            continue;
        }

        GsDomainInfo* ddii = did2dinfo[*it]; 
        if (ddii->gang_sched_policy.type == GANG_BEST_EFFORT_MUXING) {
            be_doms_present = true;
        }
        else if (ddii->gang_sched_policy.type == GANG_NO_MUXING) {
            // Another domain on this CPU is non-muxed. 
            printf("Domain %lu failed schedulability test. "
                   " The non-multiplexed domain %lu is assigned to the same CPU.\n",
                   domid, *it); 
            return false;
        }
        else {
            other_doms.insert(*it);
        }
    }

    for (auto it = overlappers.begin(); it != overlappers.end(); it++) {
        assert(*it != domid);
        GsDomainInfo* ddii = did2dinfo[*it]; 
        if (ddii->gang_sched_policy.type == GANG_BEST_EFFORT_MUXING) {
            be_doms_present = true;
        }
        else if (ddii->gang_sched_policy.type == GANG_NO_MUXING) {
            // One of the overlapping domains on a different CPU is non-muxed. 
            // That ain't gonna work, either!.
            printf("Domain %lu failed schedulability test due to an overlapping domain. "
                   " The non-multiplexed domain %lu is assigned to the same CPU.\n",
                   domid, *it); 
            return false;
        }
        else {
            other_doms.insert(*it);
        }
    }


    if (be_doms_present) {
        total_util += ((double)be_reserve) / 100; 
        //  Calculate collective DBF* for BE cells.
        total_dbf += __calc_dbf(be_quantum, be_basic_period, period);
    }


    for (auto it = other_doms.begin(); it != other_doms.end(); it++) {
        GsDomainInfo* ddii = did2dinfo[*it]; 
        gang_sched_policy_t& pp = ddii->gang_sched_policy;

        // Sanity check 
        assert(pp.type != GANG_NO_MUXING);
        assert(pp.type != GANG_BEST_EFFORT_MUXING);


        uint64_t __period = 0.0;
        uint64_t __active_time = 0.0;
        
        if (pp.type == GANG_TIME_TRIG_MUXING) {
            __period = pp.params.tt_muxing_params.period;
            __active_time = pp.params.tt_muxing_params.active_time;
        }
        else if (pp.type == GANG_EVENT_TRIG_MUXING) {
            __period = pp.params.et_muxing_params.period;
            __active_time = pp.params.et_muxing_params.active_time;
        }

        double __u = __get_util(__active_time, __period);
        assert(__u > 0.0 || __u <= 1.0);
        
        total_util += __u;

        total_dbf += __calc_dbf(__active_time, __period, period);
    }

    // Utilization-based schedulability condition.
    bool util_cond = !((total_util + u) >= 1.0);

    // Check the DBF schedulabity condition 
    bool dbf_cond = (period >= active_time + total_dbf);
 
     return (util_cond && dbf_cond);
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

    //assert(num_of_cpus == 0); 
    //num_of_cpus = cpu_count;

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


    did2dinfo_map_t did2dinfo;
    __populate_did2dinfo(arr, len, did2dinfo);

    cpu2dom_map_t cpu2dom;
    __populate_cpu2dom(arr, len, cpu_count, cpu2dom);

    dom2overlap_map_t dom2overlap;
    __populate_dom2overlap(arr, len, cpu_count, dom2overlap);


   //////////////////////////////////////////////////////////////////////////////// 

    double bedom_util = ((double)be_reserve) / 100; 
    std::cout << "bedom_util = " << bedom_util << std::endl;

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
        std::cout << "u = " << u << std::endl;

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
        std::cout << "total_util_per_cpu[" << cpuid << "] = " << total_util_per_cpu[cpuid] << std::endl;

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


////////////////////////////////////////////////////////////////////////////////




//    cpu2dom_map_t::iterator c2d_it;    
//    for (c2d_it = cpu2dom.begin(); c2d_it != cpu2dom.end(); c2d_it++) {
//        size_t cpuid = c2d_it->first;
//        std::set<size_t>& doms_on_cpu = c2d_it->second;
//
//        if (doms_on_cpu.empty()) {
//            // No cell runs on this CPU. 
//            continue;
//        }
//        
//        std::set<size_t>::iterator dit;
//        for (dit = doms_on_cpu.begin(); dit != doms_on_cpu.end(); dit++) {
//            size_t domid = *dit;
//            std::set<size_t>& overlappers = dom2overlap[domid];
//            
//            bool res = __test_dom(domid, 
//                                  doms_on_cpu, 
//                                  overlappers, 
//                                  be_reserve,
//                                  be_basic_period,
//                                  did2dinfo);
//
//            if (res) {
//                printf("Domain %lu failed schedulability test on CPU %lu.\n", 
//                       domid, cpuid);
//                return 1;
//            }
//        }
//    }

      return rval;




// Identify cohorts.
    //__create_cohort_arrays(cpu_count);
    //__populate_cohorts(arr, 
    //                   len, 
    //                   cpu_2_cohort, 
    //                   cohorts, 
    //                   be_doms_in_cohort,
    //                   &num_of_cohorts);

    //__sort_dominfos(arr, len);

 
// ARE_SCHEDULABLE_EXIT:
//     free_scratch_map(scratch_map);
//     __destroy_cohort_arrays();
// 
//     return rval;

}
 
 
 
// ///////////////////////////////////////////////////////////////////////////////
// // Sketched logic for Schedulability Test (from Tessellation mapper.c)
// ///////////////////////////////////////////////////////////////////////////////
// 
// bool check_allocation_4_cells(mapmux_cellinfo_t** ci_arr, size_t ci_arr_size, cell_rsrc_map_t* cell_rsrc_map) {
//     // Before this was map_cells_from_scratch(...)
// 
//     assert(cell_rsrc_map != NULL);
//     assert(ci_arr != NULL);
//     assert(ci_arr_size > 0);
//     assert(is_cell_rsrc_map_empty(cell_rsrc_map));
// 
//     assert(__check_order_4_mapping_from_scratch(ci_arr, ci_arr_size));
// 
//     dtprintf(__MM_LOC, "\n");
// 
//     bool retval = FALSE;
// 
//     scratch_rsrc_alloc_map_t* scratch_map = create_scratch_map(ci_arr, ci_arr_size); 
//     // 1st Part. We  try to map  all the  cells.
// 
// # warning Using hack to stop or not start cells with number of harts = 0.
// 
//         for (int i = 0; i < ci_arr_size; ++i) {
//             mapmux_cellinfo_t* ci = ci_arr[i];
//             if (ci->phys_rsrc_spec.pe_spec.num_of_prealloc_pes == 0) {
//                     // Skip the cell as it should no be in operation.
//                     continue;
//                 }
// 
//             if (!__map_single_cell(i, scratch_map)) {
//                     dprintf(__MM_LOC, "Couldn't map Cell %d %(= %ci_arr[%d]).\n", %ci->cell->pid, %i);
//                     goto MAP_FROM_SCRATCH_RETURN;
//                 }
//         }
// 
//     retval = TRUE;
// 
// MAP_FROM_SCRATCH_RETURN:
//     free_scratch_map(scratch_map);
//     return retval;
// 
// }
// 
// 
// 
// /**
//  * Attempts to map a single cell.
//  *
//  * @parem cell_idx Index of the considered cell in the scratch resource
//  * allocation map.
//  * @param scratch_map The scratch resource-allocation map.
//  * @returns TRUE if it succeeds; FALSE otherwise.
//  */
// static bool __map_single_cell(int32_t cell_idx, scratch_rsrc_alloc_map_t* scratch_map) {
// 
//     assert(scratch_map != NULL);
//     assert(cell_idx < scratch_map->ci_arr_size);
// 
//     mapmux_cellinfo_t* ci = scratch_map->ci_arr[cell_idx];
//     //    dprintf(__MM_LOC, "Mapping cell %d\n", ci->cell->pid);
// 
//     bool retval = FALSE;
// 
//     // Check for number of memory pages.
//     int32_t req_pages =
//         scratch_map->allocated_mem_pages +
//         ci->phys_rsrc_spec.num_of_phys_pages;
// 
//     if (req_pages < 0 && req_pages >
//             scratch_map->total_mem_pages_4_cells) {
//         dprintf(__MM_LOC, "Couldn't meet
//                 memory requirements of Cell %d.\n",
//                 ci->cell->pid);
//         goto MAP_SINGLE_CELL_RETURN;
//     }
// 
//     // Map hardware
//     // threads.
//     retval =
//         __alloc_resources_in_map(cell_idx,
//                 HART,
//                 &(ci->phys_rsrc_spec.pe_spec.prealloc_pes),
//                 ci->phys_rsrc_spec.pe_spec.num_of_prealloc_pes,
//                 scratch_map);
// 
// 
// 
//     dprintf(__MM_LOC, "Hadware threads mapped successfully for cell %d.\n", ci->cell->pid);
// 
// 
//     update_in_scratch_map(cell_idx,
//             ci->phys_rsrc_spec.pe_spec.prealloc_pes,
//             ci->phys_rsrc_spec.pe_spec.num_of_prealloc_pes,
//             ci->phys_rsrc_spec.cache_unit_spec.cache_unit_ids,
//             ci->phys_rsrc_spec.cache_unit_spec.num_of_cache_units,
//             membw_units,
//             membw_unit_count,
//             req_pages,
//             scratch_map);
// 
// MAP_SINGLE_CELL_RETURN:
//     return
//         retval;
// 
// }
// 
// 
// 
// 
// /**
//  * Checks if the requested CPUs allocation is schedulable.
//  *
//  * @parem cell_idx Index of the considered cell in the scratch resource allocation map.
//  * @param rsrc_type Type of the resource being considered.
//  * @param[in,out] req_rsrcs Array of indices of the specific resource elements being considered.
//  * @param req_rsrcs_count Size of the array 'req_rsrcs'.
//  * @param scratch_map The scratch resource-allocation map.
//  *
//  * @returns TRUE if a schedulable allocation was found; FALSE otherwise.
//  */
// static bool __alloc_resources_in_map(int32_t cell_idx,
//                                       phy_rscr_type_t rsrc_type,
//                                       int32_t** req_rsrcs,
//                                       uint32_t req_rsrcs_count,
//                                       scratch_rsrc_alloc_map_t* scratch_map)
// {
//    mapmux_cellinfo_t* ci = scratch_map->ci_arr[cell_idx];
// 
//    int32_t req_util = get_cell_util(&ci->tm_muxing_spec);
// 
//    rscr_bitmap_array_t* bitmap_arr = &(scratch_map->rscr_bitmap_arrays[rsrc_type]);
// 
// 
//    if (are_suitable_elements(bitmap_arr, req_util, *req_rsrcs, req_rsrcs_count) &&
//        test_schedulability(cell_idx, rsrc_type, *req_rsrcs, req_rsrcs_count, scratch_map)) {
//        return TRUE;
//    }
//    else {
//             return FALSE;
//     }
// 
// }
// 
// 
// 
// 
// ///////////////////////////////////////////////////////////////////////////////
// // Functions related to Schedulability Test (from Tessellation mapper_helper.c)
// ///////////////////////////////////////////////////////////////////////////////
// 
// void get_suitable_elements(rscr_bitmap_array_t* bitmap_arr, const int32_t req_util,
//                            int32_t** elems, size_t* num_of_elems) {
//     assert(req_util >= 0);
// 
//     size_t sz = sizeof(int32_t) * bitmap_arr->size;
//     int32_t* __elems = kmalloc(sz, 0);
//     memset(__elems, 0, sz);
// 
//     size_t __num_of_elems = 0;
// 
//     for (size_t i = 0; i < bitmap_arr->size; ++i) {
//         int32_t tmp_util = (bitmap_arr->util_arr[i] + req_util);
//         if (tmp_util <= MAX_UTILIZATION) {
//             __elems[__num_of_elems++] = i;
//         }
//     }
// 
//     if (__num_of_elems == 0) {
//         kfree(__elems);
//         __elems = NULL;
//     }
// 
//     *num_of_elems = __num_of_elems;
//     *elems = __elems;
// }
// 
// 
// bool are_suitable_elements(rscr_bitmap_array_t* bitmap_arr, int32_t req_util,
//                            int32_t* elems, size_t num_of_elems) {
//     assert(req_util >= 0);
//     assert(bitmap_arr->size >= num_of_elems);
// 
//     for (size_t i = 0; i < num_of_elems; ++i) {
//         int32_t idx = elems[i];
//         int32_t tmp_util = (bitmap_arr->util_arr[idx] + req_util);
//         if (tmp_util > MAX_UTILIZATION) {
//             return FALSE;
//         }
//     }
//     return TRUE;
// }
// 
// 
// int32_t get_util(uint64_t period, uint64_t active_time) {
//     if(active_time > period) {
//         return -1; // Wrong values.
//     }
// 
//     if(active_time == period) {
//         return MAX_UTILIZATION; // 100% percent of utilization.
//     }
// 
//     int64_t num = MAX_UTILIZATION * active_time;
//     int64_t u = (num % period) ? ((num / period) + 1) : (num / period);
// 
//     if (u < MIN_UTILIZATION) {
//         return MIN_UTILIZATION;
//     }
// 
//     return (int32_t) u;
// }
// 
// 
// int32_t get_cell_util(tm_muxing_spec_t* tm_spec) {
//     assert(tm_spec != NULL);
//     switch (tm_spec->type) {
//         case CELL_NO_MUXING:
//             return MAX_UTILIZATION;
//         case CELL_TIME_TRIG_MUXING:
//             return get_util(tm_spec->params.tt_muxing_params.period,
//                             tm_spec->params.tt_muxing_params.active_time);
//         case CELL_EVENT_TRIG_MUXING:
//             return get_util(tm_spec->params.et_muxing_params.period,
//                             tm_spec->params.et_muxing_params.active_time);
//         case CELL_BEST_EFFORT_MUXING:
//             return RESERVED_UTIL_FOR_A_BE_CELL;
//         default:
//              panic("Invalid time-multiplexing policy");
//     }
// }
// 
// 
// uint64_t get_cell_period(tm_muxing_spec_t* tm_spec) {
//     switch (tm_spec->type) {
//         case CELL_NO_MUXING:
//             return INFINITY;
//         case CELL_BEST_EFFORT_MUXING:
//             return 0;
//         case CELL_TIME_TRIG_MUXING:
//             return tm_spec->params.tt_muxing_params.period;
//         case CELL_EVENT_TRIG_MUXING:
//             return tm_spec->params.et_muxing_params.period;
//         default:
//              panic("Invalid time-multiplexing policy");
//     }
// }
// 
// 
// int64_t get_cell_active_time(tm_muxing_spec_t* tm_spec) {
//     switch (tm_spec->type) {
//         case CELL_NO_MUXING:
//             return INFINITY;
//         case CELL_BEST_EFFORT_MUXING:
//             return 0;
//         case CELL_TIME_TRIG_MUXING:
//             return tm_spec->params.tt_muxing_params.active_time;
//         case CELL_EVENT_TRIG_MUXING:
//             return tm_spec->params.et_muxing_params.active_time;
//         default:
//              panic("Invalid time-multiplexing policy");
//     }
// }
// 
// 
// /**
//  * Calculates the Demand-Bound Function (DBF*) for a specific cell.
//  * Recall that utilization is multiplied by MAX_UTILIZATION.
//  * So this function returns (DBF* x MAX_UTILIZATION).
//  *
//  * For details refer to the paper "A Comparison of Global and Partitioned EDF Schedulability
//  * Tests for Multiprocessors" by Theodore P. Baker, page 2.
//  *
//  * @param ci the cell
//  * @param time the time frame to be considered to calculate DBF*
//  * @returns the (DBF* x MAX_UTILIZATION) for the cell.
//  */
// static uint64_t __calculate_dbf(tm_muxing_spec_t* tm_spec, uint64_t time) {
// 
//     assert(time < INFINITY);
// 
//     if (time == 0) {
//         return 0;  // Period of a best effort cell.
//     }
// 
//     uint64_t dbf = 0;
//     int32_t util = 0;
// 
//     switch (tm_spec->type) {
//         case CELL_NO_MUXING:
//             return 0;
//         case CELL_BEST_EFFORT_MUXING:
//             return 0;
//         case CELL_TIME_TRIG_MUXING:
//             if (!tm_spec->params.tt_muxing_params.allow_fragments) {
//                 panic("This seems wrong\n");
//             }
//             else if (tm_spec->params.tt_muxing_params.period > time) {
//                 return 0;
//             }
//             else {
//                 util = get_cell_util(tm_spec);
//                 assert(util > 0);
// 
//                 dbf = tm_spec->params.tt_muxing_params.active_time * MAX_UTILIZATION;
//                 dbf += (time - tm_spec->params.tt_muxing_params.period) * util;
//                 return dbf;
//             }
//         case CELL_EVENT_TRIG_MUXING:
//             if (tm_spec->params.tt_muxing_params.period > time) {
//                 return 0;
//             }
//             else {
//                 util = get_cell_util(tm_spec);
//                 assert(util > 0);
//                 dbf = tm_spec->params.et_muxing_params.active_time * MAX_UTILIZATION;
//                 dbf += (time - tm_spec->params.et_muxing_params.period) * util;
//                 return dbf;
//         }
//         default:
//             panic("Invalid time-multiplexing policy");
//     }
// }
// 
// 
// /**
//  * Checks the DBF schedulabity condition.
//  * This function is consistent with __calculate_dbf(...) that actually returns
//  * (DBF* x MAX_UTILIZATION).
//  */
// static bool __check_dbf_condition(tm_muxing_spec_t* tm_spec, uint64_t total_dbf) {
// 
//     uint64_t period = 0;
//     uint64_t active_time =  0;
// 
//     switch (tm_spec->type) {
//         case CELL_NO_MUXING:
//             return (total_dbf == 0);
//         case CELL_BEST_EFFORT_MUXING:
//             return TRUE;
//         case CELL_TIME_TRIG_MUXING:
//             period = tm_spec->params.tt_muxing_params.period;
//             active_time = tm_spec->params.tt_muxing_params.active_time;
//             break;
//         case CELL_EVENT_TRIG_MUXING:
//             period = tm_spec->params.et_muxing_params.period;
//             active_time = tm_spec->params.et_muxing_params.active_time;
//             break;
//         default:
//             ltprintf(__MM_LOC, "Invalid time-multiplexing policy (%d : %s)\n",
//                      tm_spec->type, tm_muxing_pol_2_str(tm_spec->type));
//             assert(FALSE);
//             break;
//     }
//     assert(period != 0 && active_time != 0);
// 
//     return (period * MAX_UTILIZATION >= active_time * MAX_UTILIZATION + total_dbf);
// }
// 
// 
// /**
//  * Tests whether or not a given cell is schedulable on a specific resource unit
//  * considering the cells that have been already assigned to that resource unit.
//  *
//  * For a complete explanation of how this test works, refer to the paper
//  * "A Comparison of Global and Partitioned EDF Schedulability Tests for Multiprocessors"
//  * by Theodore P. Baker.
//  *
//  * @param cell_idx Index of the considered cell in the scratch resource allocation map.
//  * @param rsrc_type Type of the resource being considered.
//  * @param rsrc_idx Index of the specific resource element being considered.
//  * @param scratch_map The scratch resource-allocation map.
//  * @returns TRUE if the test succeeds; FALSE otherwise.
//  */
// static bool
// __test_schedulability_of_resource_for_cell(int32_t cell_idx,
//                                            phy_rscr_type_t rsrc_type,
//                                            int32_t rsrc_idx,
//                                            scratch_rsrc_alloc_map_t* scratch_map) {
// 
//     mapmux_cellinfo_t* tested_ci = scratch_map->ci_arr[cell_idx];
// 
//     // TODO: Support other time muxing policies.
//     // This function seems to support just divisible time-triggered and event-triggered cells!
//     // Verify that the time muxing policy is currently supported.
//     if (!is_tm_muxing_policy_supported(&tested_ci->tm_muxing_spec)) {
//         panic("[MAPMUX @ %s(...)] Not supported time-multiplexing policy. "
//               "Cell %d; Policy: %s \n",
//               __FUNCTION__,
//               tested_ci->cell->pid,
//               tm_muxing_pol_2_str(tested_ci->tm_muxing_spec.type));
//     }
// 
//     uint64_t my_period = get_cell_period(&tested_ci->tm_muxing_spec);
//     int32_t my_util = get_cell_util(&tested_ci->tm_muxing_spec);
//     assert(my_util >= 0);
//     assert(my_util <= MAX_UTILIZATION);
// 
//     size_t bitmap_size = get_bitmap_size_for_rsrc(rsrc_type, scratch_map);
//     assert(bitmap_size == scratch_map->ci_arr_size);
// 
//     uint8_t* bitmap_for_resource = get_bitmap_for_rsrc(rsrc_type, rsrc_idx, scratch_map);
// 
// //    ///////////////   DEBUGGING /////////////////
// //    dprintf(__MM_LOC, "bitmap_for_resource from scratch_map: "); // DELETE ME
// //    PRINT_BITMASK(bitmap_for_resource, bitmap_size); //  DELETE ME
// //    /////////////////////////////////////////////
// 
//     // Calculate the Demand-Bound Function (DBF) and utilization for the rest of the cells.
//     int32_t total_util = 0;
//     uint64_t total_dbf = 0;
// 
//     for (int i = 0; i < bitmap_size; i++) {
//         if (i != cell_idx && GET_BITMASK_BIT(bitmap_for_resource, i)) {
//             // The bit is set, the cell belongs to the cell set.
//             mapmux_cellinfo_t* ci = scratch_map->ci_arr[i];
//             assert(ci != NULL);
// 
//             int32_t util = get_cell_util(&ci->tm_muxing_spec);
//             assert(util >= 0);
//             assert(util <= MAX_UTILIZATION);
// 
//             total_util += util;
// 
//             int32_t v = total_util + my_util;
//             if (v > MAX_UTILIZATION) {
//                 // Demanding more than 100% utilization. No need to proceed.
//                 dprintf(__MM_LOC, "total_util = %d, my_util = %d, "
//                         "(total_util + my_util) = %d > MAX_UTILIZATION = %d \n",
//                         total_util, my_util, v, MAX_UTILIZATION);
//                 return FALSE;
//             }
// 
//             int64_t dbf = __calculate_dbf(&ci->tm_muxing_spec, my_period);
//             total_dbf += dbf;
//         }
//     }
// 
//     bool util_cond = !((total_util + my_util) > MAX_UTILIZATION);
// 
//     bool dbf_cond = __check_dbf_condition(&tested_ci->tm_muxing_spec, total_dbf);
// 
//     // For debugging
//     // dprintf(__MM_LOC, "util_cond = %d; dbf_cond = %d \n", util_cond, dbf_cond);
// 
//     return (util_cond && dbf_cond);
// }
// 
// 
// /**
//  * Tests whether or not a set of cells are schedulable on a given resource element.
//  * For a set of cells to be schedulable, all the cells in the set must satisfy the
//  * Demand-Bound Function (DBF) and utilization properties.
//  *
//  * @param rsrc_type Type of the resource being considered.
//  * @param rsrc_idx Index of the specific resource element being considered.
//  * @param scratch_map The scratch resource-allocation map.
//  * @returns TRUE if the test succeeds; FALSE otherwise.
//  */
// static bool __test_sched_of_rsrc(phy_rscr_type_t rsrc_type, int32_t rsrc_idx,
//                                  scratch_rsrc_alloc_map_t* scratch_map) {
// 
//     size_t bitmap_size = get_bitmap_size_for_rsrc(rsrc_type, scratch_map);
// 
//     uint8_t* bitmap_for_resource = get_bitmap_for_rsrc(rsrc_type, rsrc_idx, scratch_map);
// 
//     for (int i = 0 ; i < bitmap_size; i++) {
//         if (GET_BITMASK_BIT(bitmap_for_resource, i)) {
//             bool result =
//                 __test_schedulability_of_resource_for_cell(i, rsrc_type, rsrc_idx, scratch_map);
//             if (!result) {
//                 mapmux_cellinfo_t* ci = scratch_map->ci_arr[i];
//                 dprintf(__MM_LOC, "Cell %d is not schedulable on resource %s (id = %d). \n",
//                         ci->cell->pid, mapped_rsc_type_2_str(rsrc_type), rsrc_idx);
//                 return FALSE;
//             }
//         }
//     }
// 
//     return TRUE;
// }
// 
// 
// bool test_schedulability(int32_t cell_idx,
//                          phy_rscr_type_t rsrc_type,
//                          int32_t* req_rsrcs,
//                          uint32_t req_rsrcs_count,
//                          scratch_rsrc_alloc_map_t* scratch_map) {
// 
//     // Test schedulability for each required resource of the give type.
//     for (int i = 0; i < req_rsrcs_count; i++) {
//         int32_t rsrc_idx = req_rsrcs[i];
//         assert(rsrc_idx < scratch_map->rscr_bitmap_arrays[rsrc_type].size);
// 
//         uint8_t* bitmap_for_resource = get_bitmap_for_rsrc(rsrc_type, rsrc_idx, scratch_map);
// 
//         // Temporarily set the bit corresponding to 'cell_idx'.
//         SET_BITMASK_BIT(bitmap_for_resource, cell_idx);
// 
//         bool result = __test_sched_of_rsrc(rsrc_type, rsrc_idx, scratch_map);
// 
//         // Clear the bit before returning.
//         CLR_BITMASK_BIT(bitmap_for_resource, cell_idx);
// 
//         if (!result) {
//             return FALSE;
//         }
//     }
// 
//     // We are here, so the schedulability test succeeded for each resource.
//     mapmux_cellinfo_t* ci = scratch_map->ci_arr[cell_idx];
// 
// //    dprintf(__MM_LOC, "Cell %d is schedulable on %d resource(s) of type %s. \n",
// //            ci->cell->pid, req_rsrcs_count, mapped_rsc_type_2_str(rsrc_type));
// 
// //    dump_int_array(req_rsrcs, req_rsrcs_count);
// 
//     return TRUE;
// }
// 
// 






///////////////////////////////////////////////////////////////////////////////
// Functions related to sorting cell_info items for mapping from scratch
///////////////////////////////////////////////////////////////////////////////

// 
// // # define MAX_MAPPING_ORDINAL (2 * NUM_OF_TIME_MUXING_POLICIES - 1)
// 
// /**
//  * Returns the mapping ordinal of a cell relevant when mapping an entire cell
//  * set from scratch.
//  * It indicates the order expected by the function map_cells_from_scratch(...).
//  */
// static __inline
// int32_t get_ordinal_4_mapping_from_scratch(phys_rsrc_spec_t* phys_rsrc_spec,
//         tm_muxing_spec_t* tm_muxing_spec)
// {
//     assert(phys_rsrc_spec != NULL); 
//     assert(tm_muxing_spec != NULL);
//     assert(phys_rsrc_spec->pe_spec.prealloc_pes != NULL);
// 
// 
//     return tm_muxing_spec->type;
// 
//     // TODO: CPUs must always be specified.
// 
//     //if (phys_rsrc_spec->pe_spec.prealloc_pes != NULL) {
//     //        return tm_muxing_spec->type;
//     //    }
//     //else { 
//     //    return NUM_OF_TIME_MUXING_POLICIES + tm_muxing_spec->type;
//     //}
// }

 
// /**
//  * Sorts the cellinfo items according the expected order by the mapper when mapping
//  * from scratch.  That is, this is the order expected by the function
//  * map_cells_from_scratch(...).
//  */
// void sort_cellinfos_4_mapping_from_scratch(mapmux_cellinfo_t** arr, size_t size) {
//     // TODO: Improve efficiency; we use bubblesort here.
//     size_t i = 0;
//     mapmux_cellinfo_t* tmp = NULL;
//     while(i != size - 1) {
//         int32_t item_ord = get_ordinal_4_mapping_from_scratch(&(arr[i]->phys_rsrc_spec), &(arr[i]->tm_muxing_spec));
//         int32_t
//             next_item_ord = get_ordinal_4_mapping_from_scratch(&(arr[i+1]->phys_rsrc_spec), &(arr[i+1]->tm_muxing_spec));
//         if (item_ord > next_item_ord)  { 
//             tmp = arr[i];
//             arr[i] = arr[i+1];
//             arr[i+1] = tmp;
//             i = 0;
//         }
//         else
//         {
//             i++;
//         }
//     }
// }
