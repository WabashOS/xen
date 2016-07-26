/*
 * File: common/sched_gang.c
 * Author: Juan A. Colmenares <juancol@eecs.berkeley.edu>
 *
 * Description: An integrated gang scheduler for Xen.
 *
 * Copyright (C) 2014 - Juan A. Colmenares
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define __XEN_TOOLS__ /* for using get_xen_guest_handle macro */

#include <xen/config.h>
#include <xen/lib.h>
#include <xen/sched.h>
#include <xen/sched-if.h>
#include <xen/timer.h>
#include <xen/softirq.h>
#include <xen/errno.h>
#include <xen/time.h>
#include <xen/watchdog.h>
#include <xen/delay.h>
#include <xen/event.h>

#include <xen/hashtable.h> 
#include <xen/hashtable_itr.h> 
#include <extras/tree.h>

#include <xen/guest_access.h>

#include <xen/console.h>

///////////////////////////////////////////////////////////////////////////////
// TO-DO Notes
///////////////////////////////////////////////////////////////////////////////

// REQUIRED FEATURES IN PROGRESS.

// TODO: Tune DEFAULT_ADJ_TIME_UPPER_BOUND. 
//       Current value is very conservative; the expected value = 1ms.


// TODO: Initially consider to panic if the following functions are called:
// - set_node_affinity 
// - migrate
// - pick_cpu
// They are likely to break the gang scheduler, but can we leverage those
// functions for adaptation in the gang scheduling?


// TODO: Add support for gang versions of yielding, blocking, and polling. 


// OPTIMIZATIONS

// FIXME: Fix function __add_dom_to_muxgroups(...) and use mux groups again. 
// This is not critical since mux groups are an optimization. 


// TODO: In do_sched(...), break if checked_mux_group == mux_groups in the cohort.

// TODO: Use cache-friendly alignments in per_cpu_sched_info (optimization). 



// ADDITIONAL FUNCTIONALITY

// TODO: Support weighted round-robin for best-effort domains.
// Related FILE: "include/public/gang_sched_policies.h" 

// TODO: Implement no-fragmentable time-triggered gang scheduling policy
// Related FILE: "include/public/gang_sched_policies.h" 

///////////////////////////////////////////////////////////////////////////////


#ifndef NDEBUG
#define GANG_STATS
#define CHECK(_p)                                           \
    do {                                                    \
        if ( !(_p) )                                        \
            printk("Check '%s' failed, line %d, file %s\n", \
                   #_p , __LINE__, __FILE__);               \
    } while(0)
#else
#define CHECK(_p) ((void)0)
#endif


#ifndef __PANIC
#define __PANIC(_msg)                              \
    do {                                           \
      panic("[ERROR @ line: %d, file: %s] %s \n",  \
            __LINE__, __FILE__, _msg);             \
    } while(0)     
#else
  #error "Macro '__PANIC' is already defined."
#endif


/** Prints log message. */
#ifndef GANG_LOG
#define GANG_LOG(fmt, ...)                                          \
    do {                                                            \
        /*console_start_sync();*/                                       \
        printk("[GANG_SCHED @ %s() on cpu %d] " fmt,                \
               __FUNCTION__, smp_processor_id(), ##__VA_ARGS__);    \
        /*console_end_sync();*/                                         \
    } while(0)                                                      
#else
  #error "Macro 'GANG_LOG' is already defined."
#endif


/** Prints log message with time. */
#ifndef GANG_LOGT
#define GANG_LOGT(fmt, ...)                                                     \
    do {                                                                        \
        /*console_start_sync();*/                                                   \
        printk("[GANG_SCHED @ %s() on cpu %d @ t = %ld us] " fmt,               \
               __FUNCTION__, smp_processor_id(), get_s_time(), ##__VA_ARGS__);  \
        /*console_end_sync();*/                                                    \
    } while(0)                                                      
#else
  #error "Macro 'GANG_LOGT' is already defined."
#endif


#define SUPPRESS_NOT_USED_WARN __attribute__((unused))


///////////////////////////////////////////////////////////////////////////////
// Macros for converting time values to nanoseconds, which is the base unit Xen
// uses (see include/xen/time.h). 
///////////////////////////////////////////////////////////////////////////////

// We give clearer names to Xen's time conversion macros, defined in
// include/xen/time.h.

/** Convert seconds to nanoseconds. */
#define s_2_ns(_s)  (SECONDS(_s))

/** Convert milliseconds to nanoseconds. */
#define ms_2_ns(_s) (MILLISECS(_s))

/** Convert microseconds to nanoseconds. */
#define us_2_ns(_s) (MICROSECS(_s))


///////////////////////////////////////////////////////////////////////////////
// Basic constants.
///////////////////////////////////////////////////////////////////////////////

#ifndef TRUE
    #define TRUE  (1)
    #define FALSE (0)
#endif

/** 
 * Minimum time slice at boot time in microseconds.
 * This is used mostly to validate the parameters of time multiplexing policies.
 * Xen's default value is 1 ms.
 */
#define GANG_FINEST_TIME_GRAIN_IN_US (sched_ratelimit_us)

/** Minimum time slice in nanoseconds. */
#define GANG_FINEST_TIME_GRAIN (us_2_ns(GANG_FINEST_TIME_GRAIN_IN_US))

/** 
 * Margin to consider the remaining time effectively equal to zero. 
 * This time value is in nanoseconds.
 */
static const s_time_t MARGIN = us_2_ns(10);

/** Infinity. */
#define INFINITY (STIME_MAX)

/** */
static const unsigned long BARRIER_SPIN_DELAY_IN_US = 20;

// Bits in the flag.

/** Individual VCPU yielding. */
#define GSBIT_SINGLE_VCPU_YIELD        (1)

/** Individual VCPU has been put to sleep. */
#define GSBIT_IS_SLEEPING              (2)

/** The VCPU just woke up and came out from the waiting-for-event set. */
#define GSBIT_WAS_WAITING_FOR_EVENT    (3)


///////////////////////////////////////////////////////////////////////////////
// Default values and boot parameters for best-effort domains.
///////////////////////////////////////////////////////////////////////////////

/** 
 * Default value for the reserved CPU utilization, in percentage, for
 * best-effort domains. 
 */
#define DEFAULT_CPU_RESERV_4_BE_DOMAINS (10)

#define MIN_CPU_RESERV_4_BE_DOMAINS (0)

#define MAX_CPU_RESERV_4_BE_DOMAINS (100)


/** 
 * Boot parameter indicating the reserved CPU utilization, in percentage, for
 * best-effort domains. 
 */
SUPPRESS_NOT_USED_WARN
static uint8_t __read_mostly 
sched_gang_cpu_rsrv_4_be_doms = DEFAULT_CPU_RESERV_4_BE_DOMAINS;
integer_param("sched_gang_cpu_rsrv_4_be_doms", sched_gang_cpu_rsrv_4_be_doms);


/** Minimun and default period for best-effort domains in milliseconds. */
#define DEFAULT_PERIOD_4_BE_DOMAINS (ms_2_ns(100))

/**
 * Boot parameter indicating the basic period for best-effort domains (in
 * nanoseconds).
 * It is used to derived the quantum for best-effort domains. 
 */
SUPPRESS_NOT_USED_WARN
static s_time_t __read_mostly 
sched_gang_period_4_be_doms = DEFAULT_PERIOD_4_BE_DOMAINS;

// FIXME: Add user-provided parameter in milliseconds.
//        Need to be set to an internal variable in nanoseconds.
//integer_param("sched_gang_period_4_be_doms", sched_gang_period_4_be_doms);


/**
 * The period for best-effort domains (in ns)
 * This variable is set based on the boot parameter and actually used. 
 */
SUPPRESS_NOT_USED_WARN
static s_time_t __read_mostly 
__period_4_be_doms = DEFAULT_PERIOD_4_BE_DOMAINS;


/** Default quantum for best-effort domains (in ns). */
#define DEFAULT_QUANTUM_4_BE_DOMAINS \
    ((DEFAULT_PERIOD_4_BE_DOMAINS * DEFAULT_CPU_RESERV_4_BE_DOMAINS) / 100)

/** 
 * Quantum for best-effort domains (in ns).
 * This variable is set based on the boot parameters and actually used. 
 */
SUPPRESS_NOT_USED_WARN
static s_time_t __read_mostly 
__quantum_4_be_doms = DEFAULT_QUANTUM_4_BE_DOMAINS;


////////////////////////////////////////////////////////////////////////////////
// Default values and boot parameter related to the time upper bound for global
// adjustment of CPUs and time multiplexing parameters (i.e., resource
// redistribution).
////////////////////////////////////////////////////////////////////////////////

/** 
 * Minimum value for the time upper bound for global adjustment (in
 * nanoseconds).
 */
#define MIN_ADJ_TIME_UPPER_BOUND (ms_2_ns(10))

/** 
 * Maximum value for the time upper bound for global adjustment (in
 * nanoseconds).
 */
#define MAX_ADJ_TIME_UPPER_BOUND (ms_2_ns(500))

/** 
 * Default value for the time upper bound for global adjustment (in
 * nanoseconds).
 */
#define DEFAULT_ADJ_TIME_UPPER_BOUND (ms_2_ns(100))

/** 
 * Boot parameter indicating the time upper bound for global adjustment (in
 * nanoseconds).
 * @note{Be careful when setting this parameter. Make sure you know what you are
 * doing.}
 */
SUPPRESS_NOT_USED_WARN
static s_time_t __read_mostly 
sched_gang_adj_time_upper_bound = DEFAULT_ADJ_TIME_UPPER_BOUND;

// TODO: Add user-provided parameter in milliseconds.
//       Need to be set to an internal variable in nanoseconds.
//integer_param("sched_gang_adj_time_upper_bound",
//              sched_gang_adj_time_upper_bound);


/** 
 * Adopted upper bound for the time that it takes to complete a global
 * adjustment of CPUs and time multiplexing parameters (i.e., resource
 * redistribution). In nanoseconds.
 */
static s_time_t __read_mostly 
__adj_time_upper_bound = DEFAULT_ADJ_TIME_UPPER_BOUND;

///////////////////////////////////////////////////////////////////////////////
// General auxiliary functions and macros.
///////////////////////////////////////////////////////////////////////////////

#define GANG_PRIV_DATA(_ops) \
    ((struct gang_priv_data*)((_ops)->sched_data))

#define GANG_PER_CPU_INFO(_cpuid) \
    ((struct gang_pcpu_info*) (per_cpu(schedule_data, _cpuid).sched_priv))

#define LOCAL_SCHED_INFO(_cpuid) \
    (GANG_PER_CPU_INFO(_cpuid)->local_sched)

#define SCHED_TICKET(_vcpu) \
    ((struct sched_ticket*) (_vcpu)->sched_priv)

#define DOMAIN_SCHED_INFO(_d) \
    ((struct gang_dom_info*) (_d->sched_priv))


///////////////////////////////////////////////////////////////////////////////
// Scheduler's global private data.
///////////////////////////////////////////////////////////////////////////////
/** 
 * Gang scheduler's global private data. 
 */
typedef struct gang_priv_data {

    /** Lock for the whole pluggable scheduler, nests inside cpupool_lock. */
    spinlock_t lock;

} gang_priv_data_t;

//////////////////////////////////////////////////////////////////////////// 
// HARDWARE THREAD COHORTS
//
// A 'domain cohort' is a group of domains in which a domain overlaps with at
// least another domain on one or more hardware threads.
// A 'hardware thread cohort' includes the hardware threads allocated to the
// domains that form a domain cohort.
//
// The notion of cohort is important because, for correct gang scheduling,
// the local schedulers residing on the hardware threads of a cohort need to
// maintain an identical view of the state of that portion of the system.
//////////////////////////////////////////////////////////////////////////// 

/** 
 * Array that indicates the membership of each hardware thread (here CPU) to
 * a cohort. 
 * That is, the i-th element in the array stores the ID of the cohort the
 * i-th hardware thread in the system belongs to.  If the i-th element is
 * negative, then the i-th hardware thread belongs to no cohort
 *
 * Valid cohort indexes can go from 0 to (NR_CPUS - 1).
 *
 * The size of the array is the number of hardware threads in the systems.
 */ 
SUPPRESS_NOT_USED_WARN
static __read_mostly 
int cpu_2_cohort[NR_CPUS] = { [0 ... (NR_CPUS - 1)] (-1) };

/** 
 * Array of bitmasks indicating which hardware threads (here CPUs) belong to
 * each cohort. 
 * The size of the array is the number of hardware threads in the system.
 * Note that (the number of cohorts) <= (the number of hardware threads).
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly 
cpumask_t cohorts[NR_CPUS];


/**
 * Array that indicates the number of best-effort domains in each cohort.
 * The size of the array is the number of hardware threads in the systems.
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly
int be_doms_in_cohort[NR_CPUS] = { 0 };

/** 
 * Number of existing cohorts.
 * It can go from 0 to the number of hardware threads in the system. 
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly 
size_t num_of_cohorts = 0; 


// FIXME: MULTIPLEXING (MUX) GROUPS are not used for the moment. 

//////////////////////////////////////////////////////////////////////////// 
// MULTIPLEXING (MUX) GROUPS
// 
// A mux group comprises hardware threads that have *exacly* the same set of
// domains assigned to them. Thus, two hardware threads in the same mux
// group produce the same schedule and activate domains in the same manner. 
// 
// Mux groups are mostly a convenient optimization since schedule decisions
// for all the hardware threads in a mux group needs to be computed once. 
//
// Note that hardware thread cohort and mux group are different, yet related
// grouping concepts. Hardware threads in the same mux group must
// belong to the same cohort; the contrary is not generaly true.  
//////////////////////////////////////////////////////////////////////////// 

/** 
 * Array that indicates the membership of each hardware thread (here CPU) to
 * a multiplexing (mux) group. 
 * That is, the i-th element in the array stores the ID of the mux group the
 * i-th hardware thread in the system belongs to.  If the i-th element is
 * negative, then the i-th hardware thread belongs to no mux group.
 *
 * Valid mux group indexes can go from 0 to (NR_CPUS - 1).
 *
 * The size of the array is the number of hardware threads in the systems.
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly 
int cpu_2_muxgroup[NR_CPUS] = { [0 ... (NR_CPUS - 1)] (-1) };

/** 
 * Array of bitmasks indicating which hardware threads (here CPUs) belong to
 * each mux group. 
 * The size of the array is the number of hardware threads in the system.
 * Note that (the number of mux groups) <= (the number of hardware threads).
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly 
cpumask_t muxgroups[NR_CPUS];

/**
 * Number of existing multiplexing groups.
 * It can go from 0 to the number of hardware threads in the system. 
 */
SUPPRESS_NOT_USED_WARN
static __read_mostly 
size_t num_of_muxgroups = 0; 

///////////////////////////////////////////////////////////////////////////////
// Scheduling data per domain.
///////////////////////////////////////////////////////////////////////////////
/** 
 * Scheduling data for individual domain.  
 * It contains information of a domain about its resource mapping and
 * time-multiplexing. That information comprises:
 * - the specification of the concrete resources assigned to the domain, and 
 * - the specification of the time-multiplexing policy for the domain.
 */
typedef struct gang_dom_info {

    struct domain* domain;

    ////////////////////////////////////////////////////////////////////////////
    // Physical resources assigned to the domain.
    ////////////////////////////////////////////////////////////////////////////

    // TODO: Consider other physical resources, conventional (e.g., memory
    // pages) and unconventional (e.g., partioned cached units, bandwidth to
    // external memory).
    // So far, we just started with the obvious ones: hardware threads.

    /** 
     * Mask indicating the hardware threads assigned (pre-allocated) to this
     * domain. 
     */ 
    cpumask_t cpumask;

    ////////////////////////////////////////////////////////////////////////////
    // Time-multiplexing parameters for the domain.
    ////////////////////////////////////////////////////////////////////////////

    /** Data structure with the time-multiplexing parameters. */
    gang_sched_policy_t tm_muxing_spec;

    /** ID of the multiplexing cohort which this domain is member of. */
    int cohort; 

    /** 
     * Mask indicating the multiplexing groups to which this domain belong.
     */ 
    cpumask_t muxgroup_mask;

} gang_dom_info_t;

/** Returns the ID of the domain from the gang-scheduler's domain info. */
static inline
domid_t __get_domid_from_dom_info(gang_dom_info_t* di) {
    return di->domain->domain_id;
}

///////////////////////////////////////////////////////////////////////////////
// Scheduling ticket for Virtual CPU.
///////////////////////////////////////////////////////////////////////////////
/** 
 * Ticket for scheduling a domain's virtual CPU. 
 * It is a priority-queue node and contains the scheduling bookkeeping
 * information.
 */
typedef struct sched_ticket {

    /** The virtual CPU for this ticket (up pointer). */
    struct vcpu* vcpu;
  
    /** Scheduling data of the associated domain. */
    gang_dom_info_t* dom_info;

    /** Red-black tree link pointers for the EDF queue. */
    RB_ENTRY(sched_ticket) edf_queue_links;

    /** Red-black tree link pointers for the activation queue. */
    RB_ENTRY(sched_ticket) actv_queue_links;

    // Bookkeeping variables.

    /** 
     * Earliest start time for a given domain activation; this is an absolute
     * time value. (in ns)
     */
    s_time_t earliest_start_time;

    /** Absolute deadline for the activation. (in ns)*/
    s_time_t deadline;

    /** Remaining time for this activation. (in ns)*/
    s_time_t remaining_time;

    /** 
     * Time when the domain was activated (in ns).
     * It is used to calculate time spent active, which remaining_time needs to
     * know.
     */
    s_time_t activated_at;

    /** Flags that inidicates status or transitions. */
    unsigned flags;

    /**
     * Selected ticket scheduled on behalf of this ticket due to a guest
     * scheduler command BLOCK, POLL, or YIELD on a single VCPU of the domain.
     */
    struct sched_ticket* on_my_behalf;

} sched_ticket_t;

///////////////////////////////////////////////////////////////////////////////
// Priority queues based on red-black trees and related functions.
///////////////////////////////////////////////////////////////////////////////

/** Declaration of type 'struct edf_queue'. */
RB_HEAD(edf_queue, sched_ticket);
typedef struct edf_queue edf_queue_t;

/** Declaration of type 'struct actv_queue'. */
RB_HEAD(actv_queue, sched_ticket);
typedef struct actv_queue actv_queue_t;

/** Compares scheduling tickets by deadline. */
static inline 
int __cmp_tckt_deadlines(sched_ticket_t* t1, sched_ticket_t* t2) {
    if (t1->deadline < t2->deadline) {
        return -1;
    }
    else if (t1->deadline > t2->deadline) {
        return 1;
    }
    else { // t1->deadline == t2->deadline
        if (__get_domid_from_dom_info(t1->dom_info) < __get_domid_from_dom_info(t2->dom_info)) {
            return -1;
        }
        else if (__get_domid_from_dom_info(t1->dom_info) > __get_domid_from_dom_info(t2->dom_info)) {
            return 1;
        }
        return 0;
    }
}

/** Compares scheduling tickets by earliest activation time.  */
static inline 
int __cmp_tckt_rel_times(sched_ticket_t* t1, sched_ticket_t* t2) {
    if (t1->earliest_start_time < t2->earliest_start_time) {
        return -1;
    }
    else if (t1->earliest_start_time > t2->earliest_start_time) {
        return 1;
    }
    else { // t1->earliest_start_time == t2->earliest_start_time
        if (__get_domid_from_dom_info(t1->dom_info) < __get_domid_from_dom_info(t2->dom_info)) {
            return -1;
        }
        else if (__get_domid_from_dom_info(t1->dom_info) > __get_domid_from_dom_info(t2->dom_info)) {
            return 1;
        }
        return 0;
    }
}

/* Initialize red-black trees for earliest deadline first queue */
RB_GENERATE(edf_queue, sched_ticket, edf_queue_links, __cmp_tckt_deadlines);

/* Initialize red-black trees for active queue */
RB_GENERATE(actv_queue, sched_ticket, actv_queue_links, __cmp_tckt_rel_times);

///////////////////////////////////////////////////////////////////////////////
// Local (per-CPU) scheduling info.
///////////////////////////////////////////////////////////////////////////////

/** Contains scheduling control variables. */
typedef struct sched_info {

    /** 
     * Runnable queue.
     * It contains tickets, each associated with a runnable VCPU and a domain,
     * sorted by absolute deadline. Ties are resolved using the domain ID.
     */
    edf_queue_t edf_runnable_Q;

    /**
     * Activation queue.
     * It contains scheduling tickets sorted by earliest start time and
     * whose earliest start time is later than current time.
     */
    actv_queue_t activation_Q;

    /**
     * Set of waiting-for-event tickets.
     *
     * This is a hashtable that contains tickets of event-triggered domains that
     * are inactive (i.e., non-runnable) and are waiting for an activation event.
     * For each (key, value) pair, the key = domain ID and value = ticket* t.
     * 
     */
    hashtable_t* tickets_waiting_4_event;

    /** 
     * Array of current tickets for the CPUs.
     * The i-th element is a pointer to the current ticket for the i-th CPU.
     * The i-th element is NULL if there is no domain to run on the CPU (i.e.,
     * an empty  time slice due to gang scheduling). 
     */
    sched_ticket_t* cur_ticket_per_cpu[NR_CPUS];


    // TODO: Add this optimization. It replaces 'cur_ticket_per_cpu'
    /** 
     * Array of current tickets for the multiplexing groups.
     * The i-th element is a pointer to the current ticket for the i-th
     * multiplexing group. 
     * The i-th element is NULL if:
     * - There is no domain to run for the multiplexing group (i.e., an empty
     *   time slice due to gang scheduling). 
     * - The i-th multiplexing group do not exits. 
     * 
     * Note that (the number of mux groups) <= (the number of hardware threads).
     */
    // sched_ticket_t* cur_ticket_per_muxgroup[NR_CPUS];

} sched_info_t;

/** Contains per-CPU scheduling control variables. */
typedef struct gang_pcpu_info {

    /** Scheduling info currently used on the local hardware thread. */
    sched_info_t* local_sched;

} gang_pcpu_info_t;

///////////////////////////////////////////////////////////////////////////////
// Auxiliary functions related to the EDF runnable queue.
///////////////////////////////////////////////////////////////////////////////

/** Initializes the EDF runnable queue. */
static inline
void __init_runnable_Q(sched_info_t* s) {
    RB_INIT(&s->edf_runnable_Q);
}

/** Tells whether or not the EDF runnable queue is empty. */
static inline
bool_t __is_runnable_Q_empty(sched_info_t* s) {
    return RB_EMPTY(&s->edf_runnable_Q);
}

/** Inserts the given ticket into the EDF runnable queue. */
static inline
void __insert_into_runnable_Q(sched_info_t* s, sched_ticket_t* t) {
    BUG_ON(t == NULL);
    RB_INSERT(edf_queue, &s->edf_runnable_Q, t);
}

/** 
  * Removes the given ticket from the EDF runnable queue. 
  * It zeroes the field 'edf_queue_links'.
  */
static inline
sched_ticket_t* __remove_from_runnable_Q(sched_info_t* s, sched_ticket_t* t) {
    sched_ticket_t* tkt; 
    BUG_ON(t == NULL);
    BUG_ON(RB_EMPTY(&s->edf_runnable_Q));

    tkt = (sched_ticket_t*) RB_REMOVE(edf_queue, &s->edf_runnable_Q, t);
    if (t == tkt) {
        memset(&(tkt->edf_queue_links), 0, sizeof(RB_ENTRY(sched_ticket)));
    }    

    return tkt; 
}

/** 
 * Returns the scheduling ticket with the earliest absolute deadline in the EDF
 * runnable queue. 
 */
static inline
sched_ticket_t* __head_of_runnable_Q(sched_info_t* s) {
    return RB_MIN(edf_queue, &s->edf_runnable_Q);
}

/** 
 * Searches for a scheduling ticket with a given domain ID in the EDF runnable
 * queue. 
 * @returns the ticket if found; otherwise, NULL.
 */
static SUPPRESS_NOT_USED_WARN
sched_ticket_t* __search_in_runnable_Q(sched_info_t* s, domid_t domid) {
    sched_ticket_t* t = NULL;
    RB_FOREACH(t, edf_queue, &s->edf_runnable_Q) {
        if (__get_domid_from_dom_info(t->dom_info) == domid) {
            return t;
        }
    }
    return NULL;
}

/** Cleans up the EDF runnable queue and frees all the tickets. */
static SUPPRESS_NOT_USED_WARN
void __clean_runnable_Q(sched_info_t* s) {
    sched_ticket_t* t = NULL;
    while ((t = RB_MAX(edf_queue, &s->edf_runnable_Q)) != NULL) {
        sched_ticket_t* tmp = __remove_from_runnable_Q(s, t);
        BUG_ON(t != tmp);
        xfree(t);
    }

    BUG_ON(!__is_runnable_Q_empty(s));
    __init_runnable_Q(s);
}

///////////////////////////////////////////////////////////////////////////////
// Auxiliary functions related to the activation queue.
///////////////////////////////////////////////////////////////////////////////

/** Initializes the activation queue. */
static inline
void __init_activation_Q(sched_info_t* s) {
    RB_INIT(&s->activation_Q);
}

/** Tells whether or not the activation queue is empty. */
static inline
bool_t __is_activation_Q_empty(sched_info_t* s) {
    return RB_EMPTY(&s->activation_Q);
}

/** Inserts the given ticket into the activation queue. */
static inline
void __insert_into_activation_Q(sched_info_t* s, sched_ticket_t* t) {
    BUG_ON(t == NULL);
    RB_INSERT(actv_queue, &s->activation_Q, t);
}

/** 
  * Removes the given ticket from the activation queue. 
  * It zeroes the field 'actv_queue_links'.
  */
static inline
sched_ticket_t* __remove_from_activation_Q(sched_info_t* s, sched_ticket_t* t) {
    sched_ticket_t* tkt; 
    BUG_ON(t == NULL);
    BUG_ON(RB_EMPTY(&s->activation_Q));

    tkt = (sched_ticket_t*) RB_REMOVE(actv_queue, &s->activation_Q, t);
    if (t == tkt) {
        memset(&(tkt->actv_queue_links), 0, sizeof(RB_ENTRY(sched_ticket)));
    }    

    return tkt; 
}

/** Returns the ticket with the earliest start time in the activation queue. */
static inline
sched_ticket_t* __head_of_activation_Q(sched_info_t* s) {
    return RB_MIN(actv_queue, &s->activation_Q);
}

/** 
 * Searches for a scheduling ticket with a given domain ID in the activation
 * queue. 
 * @returns the ticket if found; otherwise, NULL.
 */
static SUPPRESS_NOT_USED_WARN
sched_ticket_t* __search_in_activation_Q(sched_info_t* s, domid_t domid) {
    sched_ticket_t* t = NULL;
    RB_FOREACH(t, actv_queue, &s->activation_Q) {
        if (__get_domid_from_dom_info(t->dom_info) == domid) {
            return t;
        }
    }
    return NULL;
}

/** Cleans up the activation queue and frees all the tickets. */
static SUPPRESS_NOT_USED_WARN
void __clean_activation_Q(sched_info_t* s) {
     sched_ticket_t* t = NULL;
     while ((t = RB_MAX(actv_queue, &s->activation_Q)) != NULL) {
        sched_ticket_t* tmp =  __remove_from_activation_Q(s, t);
        BUG_ON(t != tmp);
        xfree(t);
     }
 
     BUG_ON(!__is_activation_Q_empty(s));
     __init_activation_Q(s);
}

///////////////////////////////////////////////////////////////////////////////
// Auxiliary functions for the set of waiting-for-event tickets.
// Note that those tickets are only for event-triggered domains.
///////////////////////////////////////////////////////////////////////////////

#define MAX_NUM_OF_DOMAINS (1 << sizeof(domid_t))

/** Creates the set of waiting-for-event tickets. */
static inline
void __create_waiting_4_event_set(sched_info_t* s) {
    BUG_ON(s->tickets_waiting_4_event != NULL);
    s->tickets_waiting_4_event =
        create_hashtable(MAX_NUM_OF_DOMAINS, generic_hash, generic_equal);
}

/** Destroys the set of waiting-for-event tickets. */
static inline
void __destroy_waiting_4_event_set(sched_info_t* s) {
    BUG_ON(s->tickets_waiting_4_event == NULL);
    BUG_ON(hashtable_count(s->tickets_waiting_4_event) != 0);

    hashtable_destroy(s->tickets_waiting_4_event);
    s->tickets_waiting_4_event = NULL;
}

/** Tells whether or not the set of waiting-for-event tickets is empty. */
static inline
bool_t __is_waiting_4_event_set_empty(sched_info_t* s) {
    return (hashtable_count(s->tickets_waiting_4_event) == 0);
}

/**
 * Searches for a scheduling ticket with the given domain ID in the set of
 * waiting-for-event tickets.
 * @returns the ticket if found; otherwise, NULL.
 */
static inline
sched_ticket_t* __search_in_waiting_4_event_set(sched_info_t* s, domid_t domid) {
    return hashtable_search(s->tickets_waiting_4_event, (void*)((uintptr_t)domid));
}

/**
 * Removes a scheduling ticket with the given domain ID from the set of
 * waiting-for-event tickets.
 * @returns the removed ticket if found; otherwise, NULL.
 */
static inline
sched_ticket_t* __remove_from_waiting_4_event_set(sched_info_t* s, domid_t domid) {
    return hashtable_remove(s->tickets_waiting_4_event, (void*)((uintptr_t)domid));
}

/**
 * Inserts the scheduling ticket (e.g., of an event-triggered domain) with a given
 * domain ID into the set of waiting-for-event tickets.
 * @returns true if success; false otherwise.
 */
static inline
bool_t __insert_into_waiting_4_event_set(sched_info_t* s, sched_ticket_t* t) {
    domid_t domid = __get_domid_from_dom_info(t->dom_info);  
    int r = hashtable_insert(s->tickets_waiting_4_event,
                            (void*)((uintptr_t)domid), 
                            (void*) t);
    return (r != 0);
}

/**
 * Cleans up the set of waiting-for-event tickets and puts all the tickets back
 * to the pool.
 */
static SUPPRESS_NOT_USED_WARN
void __clean_waiting_4_event_set(sched_info_t* s) {
    if (hashtable_count(s->tickets_waiting_4_event) > 0) {
        hashtable_itr_t* iter = hashtable_iterator(s->tickets_waiting_4_event);
        do {
            // No need to get and free the key because it is an integer (i.e., domid_t).
            sched_ticket_t* t = (sched_ticket_t*) hashtable_iterator_value(iter);
            xfree(t);
        } while (hashtable_iterator_remove(iter) != 0);
    }

    BUG_ON(hashtable_count(s->tickets_waiting_4_event) != 0);
}

///////////////////////////////////////////////////////////////////////////////

/**
 * Initializes a per-CPU scheduling info data structure.
 */
static SUPPRESS_NOT_USED_WARN
void __init_sched_info(sched_info_t* s) {
    BUG_ON(s == NULL);
    __init_runnable_Q(s);
    __init_activation_Q(s);
    __create_waiting_4_event_set(s);

    memset(s->cur_ticket_per_cpu, 0, (NR_CPUS * sizeof(sched_info_t*))); 
    //memset(s->cur_ticket_per_muxgroup, 0, (NR_CPUS * sizeof(sched_info_t*))); 
}

/**
 * Resets (de-initializes) a per-CPU scheduling info data structure.
 */
static SUPPRESS_NOT_USED_WARN
void __deinit_sched_info(sched_info_t* s, bool_t destroy_waiting_4_event_set) {
    cpumask_t cleared_cpus;

    BUG_ON(s == NULL);
    __clean_runnable_Q(s);
    __clean_activation_Q(s);
    __clean_waiting_4_event_set(s);

    if (destroy_waiting_4_event_set) {
        __destroy_waiting_4_event_set(s);
    }

    cpumask_clear(&cleared_cpus);
    for (size_t cpu = 0; cpu < NR_CPUS; cpu++) {
        if (!cpumask_test_cpu(cpu, &cleared_cpus)) {
            sched_ticket_t* tkt = s->cur_ticket_per_cpu[cpu];
            if (tkt != NULL) {
                gang_dom_info_t* dom_info = tkt->dom_info; 
                cpumask_or(&cleared_cpus, &cleared_cpus, &(dom_info->cpumask));
            }
            xfree(tkt);
        }
    }

    memset(s->cur_ticket_per_cpu, 0, (NR_CPUS * sizeof(sched_info_t*))); 


    // TODO: Use cur_ticket_per_muxgroup as an optimization. It replaces 'cur_ticket_per_cpu'
    //for (size_t i = 0; i < NR_CPUS; ++i) {
    //    // FIXME: Prevent double free using a bitmask as above.
    //    sched_ticket_t* tkt = s->cur_ticket_per_muxgroup[i];
    //    xfree(tkt);
    //}
    //memset(s->cur_ticket_per_muxgroup, 0, (NR_CPUS * sizeof(sched_info_t*))); 
}

///////////////////////////////////////////////////////////////////////////////

/**
 * Global initialization function.
 * This is the first scheduler's function Xen calls during initialization on
 * hardware thread 0. Xen calls it only once.
 */
static int gang_global_init(void) {
    // Nothing to do here, so far.
    return 0;
}

/** 
 * Initializes gang scheduler's global, private data struct. 
 *
 * This is a scheduler's function that Xen calls in second place during
 * initialization on hardware thread 0. Xen calls it only once.
 */
static int gang_init(struct scheduler* ops) {

    s_time_t timeout; // in nanoseconds

    // Minimum period for best-effort domains (in nanoseconds). 
    const s_time_t min_period_4_be_doms = (100 * GANG_FINEST_TIME_GRAIN);

    gang_priv_data_t* pd = xzalloc(gang_priv_data_t);
    if (pd == NULL) {
        return -ENOMEM; 
    }

    spin_lock_init(&pd->lock);

    ops->sched_data = pd;

    // Check and correct boot parameters for best-effort domains.
    if (sched_gang_cpu_rsrv_4_be_doms > MAX_CPU_RESERV_4_BE_DOMAINS || 
        sched_gang_cpu_rsrv_4_be_doms < MIN_CPU_RESERV_4_BE_DOMAINS) {
        printk("WARNING: 'sched_gang_cpu_rsrv_4_be_doms' outside of valid range [%d,%d].\n"
               "    Resetting to default %u\n",
               MIN_CPU_RESERV_4_BE_DOMAINS,
               MAX_CPU_RESERV_4_BE_DOMAINS,
               DEFAULT_CPU_RESERV_4_BE_DOMAINS);
        sched_gang_cpu_rsrv_4_be_doms = DEFAULT_CPU_RESERV_4_BE_DOMAINS;
    }

    if (sched_gang_period_4_be_doms < min_period_4_be_doms) {
        printk("WARNING: 'sched_gang_period_4_be_doms' is smaller than %ld ms.\n"
               "    Resetting to that value as default.\n",
               (min_period_4_be_doms / ms_2_ns(1)));
        __period_4_be_doms = min_period_4_be_doms;
    }
    else {
        __period_4_be_doms = sched_gang_period_4_be_doms;
    }

    __quantum_4_be_doms = 
        (__period_4_be_doms * sched_gang_cpu_rsrv_4_be_doms) / 100;

    GANG_LOG("Timing parameters for Best-Effort Domains: "
             "period = %ld ns, quantum = %ld ns, reservation = %d\n",
             __period_4_be_doms, __quantum_4_be_doms,
             sched_gang_cpu_rsrv_4_be_doms);

    BUG_ON(__quantum_4_be_doms < GANG_FINEST_TIME_GRAIN);

    // Check and correct boot parameters for global adjustment of CPUs and time
    // multiplexing parameters (i.e., resource redistribution). 
    if (sched_gang_adj_time_upper_bound > MAX_ADJ_TIME_UPPER_BOUND) {
        printk("WARNING: 'sched_gang_adj_time_upper_bound' larger than "
                "the expected maximum value of %ld ms. \n"
                "    Note that default value is %ld ms\n",
                MAX_ADJ_TIME_UPPER_BOUND / 1000000,
                DEFAULT_ADJ_TIME_UPPER_BOUND / 100000);
        __adj_time_upper_bound = sched_gang_adj_time_upper_bound;
    }
    else if (sched_gang_adj_time_upper_bound < MIN_ADJ_TIME_UPPER_BOUND) {
        printk("WARNING: 'sched_gang_adj_time_upper_bound' is smaller than %ld ms.\n"
                "    Resetting to default %ld ms.\n",
                MIN_ADJ_TIME_UPPER_BOUND / 1000000,
                DEFAULT_ADJ_TIME_UPPER_BOUND / 1000000);
        __adj_time_upper_bound = DEFAULT_ADJ_TIME_UPPER_BOUND;
    }
    else {
        __adj_time_upper_bound = sched_gang_adj_time_upper_bound;
    }

    // Check timeout value needed in __smp_adjust_and_pause(...) and
    // __smp_resume_after_adjust(....).  
    timeout = (__adj_time_upper_bound / ms_2_ns(1) / 2);
    if (timeout < 1) {
        printk("WARNING: 'sched_gang_adj_time_upper_bound' was too small and "
                "it has been set to 2 ms.\n");
        __adj_time_upper_bound = ms_2_ns(2);
    }

    return 0;
}

/** 
 * De-initializes and frees gang scheduler's private data struct. 
 */
static void gang_deinit(const struct scheduler* ops) {
    gang_priv_data_t* pd = GANG_PRIV_DATA(ops);
    xfree(pd);
}

/** Allocates per-CPU scheduling info. */
static void*
gang_alloc_pdata(const struct scheduler* ops, int cpu) {

    gang_pcpu_info_t* pci = xzalloc(gang_pcpu_info_t);

    sched_info_t* local_sched = xzalloc(sched_info_t);

    if (pci == NULL || local_sched == NULL) { 
        xfree(pci);
        xfree(local_sched);
        return NULL;
    }

    __init_sched_info(local_sched);
    BUG_ON(local_sched->tickets_waiting_4_event == NULL);

    pci->local_sched = local_sched;

    // Start off idling ...
    BUG_ON(!is_idle_vcpu(curr_on_cpu(cpu)));

    return (void*) pci;
}

/** Frees per-CPU scheduling info. */
static void
gang_free_pdata(const struct scheduler* ops, void* spc, int cpu) {

    gang_pcpu_info_t* pci = (gang_pcpu_info_t*) spc;
    
    if (pci == NULL) {
        return;
    }

    ASSERT(pci->local_sched == NULL);
    
    __deinit_sched_info(pci->local_sched, TRUE);
    xfree(pci->local_sched);
    pci->local_sched = NULL;

    xfree(pci);
}

/** Allocates and initializes domain's scheduling data. */
static void* 
gang_alloc_domdata(const struct scheduler* ops, struct domain* dom) {

    // Note:
    // We initialize the domain info here because sched_move_domain(...)
    // calls alloc_domdata(...), but it does not call init_domain(...).  
    // Also note that gang_init_domain(...) calls this function. 

    gang_dom_info_t* dom_info = xzalloc(gang_dom_info_t);
    if (dom_info == NULL) {
        return NULL;
    }

    dom_info->domain = dom;
    dom_info->tm_muxing_spec.type = GANG_SCHED_POLICY_NOT_SET;
    dom_info->cohort = -1;

    return dom_info; 
}

/** Frees domain's scheduling data. */
static void 
gang_free_domdata(const struct scheduler* ops, void* data) {
    gang_dom_info_t* dom_info = (gang_dom_info_t*) data;
    xfree(dom_info);
}

/** 
 * Allocates, initializes, and sets domain's scheduling data.
 * @param[in] ops Scheduler info
 * @param[in,out] dom Domain to initialize
 * @returns Standard error codes (0 on success)
 */
static int 
gang_init_domain(const struct scheduler* ops, struct domain* dom) {

    gang_dom_info_t* dom_info = NULL;

    if (dom == dom0) {
        GANG_LOG("Error: Currently the gang scheduler cannot initialize " 
                "the privileged domain (Domain 0).\n");   
        return -EINVAL; 
    }

    if (is_idle_domain(dom)) {
        return 0;
    }

    dom_info = (gang_dom_info_t*) gang_alloc_domdata(ops, dom);
    if (dom_info == NULL) {
        return -ENOMEM;
    }

    dom->sched_priv = dom_info;

    return 0;
}

/** Resets and frees domain's scheduling data. */
static void 
gang_destroy_domain(const struct scheduler* ops, struct domain* dom) {
    gang_free_domdata(ops, dom->sched_priv);
}

/** 
 * Allocates a per-VCPU scheduling ticket and initializes it with default time
 * values. 
 */
static void*
gang_alloc_vdata(const struct scheduler* ops, struct vcpu* v, void* dd) {

    sched_ticket_t* tkt = xzalloc(sched_ticket_t);

    //GANG_LOGT("\n");

    if (tkt == NULL) {
        return NULL;
    }

    tkt->vcpu = v; 
    tkt->dom_info = (gang_dom_info_t*) dd; 

    tkt->earliest_start_time = INFINITY; 
    tkt->deadline = INFINITY; 
    tkt->remaining_time = 0; 
    tkt->activated_at = INFINITY; 

    // GANG_LOGT("Done\n");

    return tkt;
}

/** Frees a per-VCPU scheduling ticket. */
static void 
gang_free_vdata(const struct scheduler* ops, void* priv) {
    xfree(priv);
}

/** 
 * Inserts the per-VCPU scheduling ticket into the waiting-for-event set of the
 * the local scheduler. 
 */
//static void 
//gang_insert_vcpu(const struct scheduler* ops, struct vcpu* vcpu) {
//
//    int cpu_id = vcpu->processor; // hardware thread ID.
//    sched_info_t* sched_info = LOCAL_SCHED_INFO(cpu_id);
//    sched_ticket_t* tkt = SCHED_TICKET(vcpu);
//
//    BUG_ON(cpu_id < 0); 
//    BUG_ON(cpu_id > nr_cpu_ids);
//    BUG_ON(tkt == NULL); 
//
//    if (is_idle_vcpu(vcpu)) {
//        // If IDLE DOMAIN, do nothing. 
//        GANG_LOGT("Idle domain.\n");
//    }
//    else {
//        // Add the ticket to the waiting-for-event set.  
//        bool_t res = __insert_into_waiting_4_event_set(sched_info, tkt);
//        GANG_LOGT("Inserted ticket into event set.\n");
//        BUG_ON(res == FALSE);
//    }
//}

/** Possible locations for a scheduling ticket. */
enum sched_ticket_locus {
    
    /** Ticket was not found. */
    SCHED_TICKET_NOT_FOUND = 0,

    /** The ticket was in the EDF runnable queue. */
    SCHED_TICKET_IN_RUNNABLE_Q = 1,

    /** The ticket was in the activation queue. */
    SCHED_TICKET_IN_ACTIVATION_Q = 2,

    /** The ticket was in the waiting for event set. */
    SCHED_TICKET_IN_WAITING_4_EVENT_SET = 3,

    /**
     * Number of possible locations for a ticket.
     * Always at the end of the enumeration.
     */
    NUM_OF_SCHED_TICKET_LOCI,
};

/** Returns the location of the scheduling ticket. */
SUPPRESS_NOT_USED_WARN
static 
int __get_ticket_location(sched_ticket_t* tkt, sched_info_t* sched_info) {

    domid_t domid = __get_domid_from_dom_info(tkt->dom_info);

    bool_t a = (__search_in_runnable_Q(sched_info, domid) != NULL);
    bool_t b = (__search_in_activation_Q(sched_info, domid) != NULL);
    bool_t c = (__search_in_waiting_4_event_set(sched_info, domid) != NULL);

    ASSERT((a && !b && !c) || (!a && b && !c) || 
           (!a && !b && c) || (!a && !b && !c)); // Ticket not found. 

    if (a) {
        return SCHED_TICKET_IN_RUNNABLE_Q; 
    }
    else if (b) {
        return SCHED_TICKET_IN_ACTIVATION_Q; 
    }
    else if (c) {
        return SCHED_TICKET_IN_WAITING_4_EVENT_SET; 
    }
    else {
        return SCHED_TICKET_NOT_FOUND;
    }

}

static void 
gang_remove_vcpu(const struct scheduler* ops, struct vcpu* vcpu) {

    int cpu_id = vcpu->processor; // hardware thread ID.
    sched_info_t* sched_info = LOCAL_SCHED_INFO(cpu_id);
    sched_ticket_t* tkt = SCHED_TICKET(vcpu);

    BUG_ON(cpu_id < 0); 
    BUG_ON(cpu_id >= nr_cpu_ids);
    BUG_ON(sched_info == NULL); 
    BUG_ON(tkt == NULL); 

    if (is_idle_vcpu(vcpu)) {
        // If IDLE DOMAIN, do nothing. 
    }
    else {
        int tloc = __get_ticket_location(tkt, sched_info);
        sched_ticket_t* t = NULL; 

        if (tloc == SCHED_TICKET_IN_RUNNABLE_Q) {
            t = __remove_from_runnable_Q(sched_info, tkt); 
        }
        else if (tloc == SCHED_TICKET_IN_ACTIVATION_Q) {
            t = __remove_from_activation_Q(sched_info, tkt); 
        }    
        else if (tloc == SCHED_TICKET_IN_WAITING_4_EVENT_SET) {
            domid_t domid = vcpu->domain->domain_id;
            t =  __remove_from_waiting_4_event_set(sched_info, domid); 
        }    
        else if (tloc == SCHED_TICKET_NOT_FOUND) {
            //__PANIC("Ticket not found. This should not happen!");
        }
        else {
            __PANIC("Unexpected result.");
        }

        ASSERT(t == tkt);        
    }
}

/**
 * Returns an adjusted activation time that is at or after the given start time,
 * according to the period.
 * @param start_at  the start time.
 * @param actv_time the original activation time.
 * @param period    the activation period.
 * @returns the potentially adjusted activation time.
 */
SUPPRESS_NOT_USED_WARN
static inline s_time_t 
__adjust_activation_time(s_time_t start_at, 
                         s_time_t actv_time, 
                         s_time_t period) {
    if (actv_time < start_at) {
        s_time_t diff = (start_at - actv_time);
        s_time_t k = (diff % period != 0) ? 
                     ((diff / period) + 1) : (diff / period);
        return (actv_time + k * period);
    }
    else {
        return actv_time;
    }
}

/**
 * Reasons for calling __update_time_in_ticket(...).
 */
enum reason_4_updating_time_in_ticket {

    NORMAL_SCHEDULING = 0,

    PAUSED_DOMAIN = 1,

    UNPAUSED_DOMAIN = 2,

    GLOBAL_ADJUST = 3,

    /**
     * Number of reasons for calling __update_time_in_ticket(...).
     * Always at the end of the enumeration.
     */
    NUM_OF_REASONS_4_UPDATING_TIME_IN_TICKET,
};

/**
 * Updates the variables (remaining_time, deadline, and earliest_start_time) of
 * the given ticket associated with a currently active domain.
 *
 * @param si sched_info data struct.
 * @param tkt scheduling ticket of the domain.
 * @param now the current time.
 * @param reason indicates the reason for calling this function. 
 */
static
void __update_times_in_ticket(sched_info_t* s, sched_ticket_t* tkt,
                              s_time_t now, int reason) {
    // TODO: Complete implementation.

    // The most negative we accept the difference of time and ticket activation
    // time (in ns)
    static const s_time_t MIN_NEGATIVE_DIFF = -10000;
   
    gang_dom_info_t* di = tkt->dom_info; 
    domid_t dom_id = __get_domid_from_dom_info(di);

    s_time_t diff; // Time difference in ns 

    BUG_ON(s == NULL);
    BUG_ON(tkt == NULL);
    BUG_ON(__search_in_runnable_Q(s, dom_id) != NULL);

    if (tkt->vcpu != NULL) {
        BUG_ON(is_idle_vcpu(tkt->vcpu));
    }
    BUG_ON(!VALIDATE_GANG_SCHED_POLICY_TYPE(di->tm_muxing_spec.type));

    BUG_ON(reason < NORMAL_SCHEDULING);
    BUG_ON(reason >= NUM_OF_REASONS_4_UPDATING_TIME_IN_TICKET);

    if (reason == NORMAL_SCHEDULING) {

        if (di->tm_muxing_spec.type == GANG_NO_MUXING) {
            // No need to update the ticket of a non-multiplexed domain.
            __insert_into_activation_Q(s, tkt);
            return;
        }

        diff = now - tkt->activated_at;

        ///////////////////////////////////////////////////////////////////////
        // Sanity check.
        // Remaining time may go below zero, but it shouldn't go much below that.
        ///////////////////////////////////////////////////////////////////////
        if (diff < MIN_NEGATIVE_DIFF) {
            GANG_LOGT("ERROR: Domain %d; "
                      "(cur_time (%ld ns) - ticket->dispatched_at (%ld ns)) < %ld ns\n",
                      dom_id, now, tkt->activated_at, (s_time_t) MIN_NEGATIVE_DIFF);
            // __dump_sched_ticket(tkt);
            BUG();
        }
        ///////////////////////////////////////////////////////////////////////

        if (diff < 0) {
            diff = 0;
        }

        tkt->remaining_time = tkt->remaining_time - diff;

        if (di->tm_muxing_spec.type == GANG_TIME_TRIG_MUXING) {
            if (tkt->remaining_time < MARGIN) {
                tkt->earliest_start_time +=
                    di->tm_muxing_spec.params.tt_muxing_params.period;
                tkt->deadline +=
                    di->tm_muxing_spec.params.tt_muxing_params.period;
                tkt->remaining_time =
                    di->tm_muxing_spec.params.tt_muxing_params.active_time;
            }
        }
        else if (di->tm_muxing_spec.type == GANG_EVENT_TRIG_MUXING) {
            if (tkt->remaining_time < MARGIN) {
                //tkt->earliest_start_time = now;
                tkt->earliest_start_time = 
                    (now/GANG_FINEST_TIME_GRAIN)*GANG_FINEST_TIME_GRAIN;
                tkt->deadline +=
                    di->tm_muxing_spec.params.et_muxing_params.period;
                tkt->remaining_time =
                    di->tm_muxing_spec.params.et_muxing_params.active_time;
            }
        }
        else if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
        
            int cohort = di->cohort;    

            if (tkt->remaining_time < MARGIN) {
                //tkt->earliest_start_time = now;
                tkt->earliest_start_time = 
                    (now/GANG_FINEST_TIME_GRAIN)*GANG_FINEST_TIME_GRAIN;
                tkt->deadline += (be_doms_in_cohort[cohort] * __period_4_be_doms);
                tkt->remaining_time = __quantum_4_be_doms;
            }
        }
        else {
            GANG_LOG("ERROR: Domain %d with invalid time-multiplexing policy.\n", 
                     dom_id);
            ASSERT(FALSE);
        }
        
        //GANG_LOGT("Inserting ticket into 'Activation Queue'. "
        //          "Domain: %d, Policy: %s\n",
        //          dom_id, GANG_SCHED_POLICY_2_STR(di->tm_muxing_spec.type));

        __insert_into_activation_Q(s, tkt);

    }
    else if (reason == PAUSED_DOMAIN) {
        GANG_LOGT("PAUSED_DOMAIN not supported yet!\n");
        BUG();
/*
        if (di->tm_muxing_spec.type == GANG_NO_MUXING) {
            // No need to update the ticket of a non-multiplexed domain.
            __insert_into_waiting_4_event_set(s, tkt);
            return;
        }

        diff = now - tkt->activated_at;

        ///////////////////////////////////////////////////////////////////////
        // Sanity check.
        // Remaining time may go below zero, but it shouldn't go much below that.
        ///////////////////////////////////////////////////////////////////////
        if (diff < MIN_NEGATIVE_DIFF) {
            GANG_LOGT("ERROR: Domain %d; "
                      "(cur_time (%ld us) - ticket->dispatched_at (%ld us)) < %ld\n",
                      dom_id, now / 1000000,
                      tkt->activated_at / 1000000,
                      (s_time_t)(MIN_NEGATIVE_DIFF / 1000000));
            // __dump_sched_ticket(tkt);
            ASSERT(FALSE);
        }
        ///////////////////////////////////////////////////////////////////////

        if (diff < 0) {
            diff = 0;
        }

        tkt->remaining_time = tkt->remaining_time - diff;

        __insert_into_waiting_4_event_set(s, tkt);
*/
    }
    else if (reason == UNPAUSED_DOMAIN) {
        GANG_LOGT("UNPAUSED_DOMAIN not supported yet!\n");
        BUG();

/* 
        if (di->tm_muxing_spec.type == GANG_NO_MUXING) {
            // No need to update the ticket of a non-multiplexed domain.
        }
        else if (di->tm_muxing_spec.type == GANG_TIME_TRIG_MUXING) {

            // Earliest start time = 
            // NEXT period boundary after (now + GANG_FINEST_TIME_GRAIN)

            s_time_t adj_t = now + GANG_FINEST_TIME_GRAIN; 
            s_time_t k = 
                adj_t / di->tm_muxing_spec.params.tt_muxing_params.period;
            if (adj_t % di->tm_muxing_spec.params.tt_muxing_params.period) {
                k++;    
            }        

            tkt->earliest_start_time = 
                k * di->tm_muxing_spec.params.tt_muxing_params.period;

            tkt->deadline = 
                tkt->earliest_start_time +
                di->tm_muxing_spec.params.tt_muxing_params.period;

            tkt->remaining_time =
                di->tm_muxing_spec.params.tt_muxing_params.active_time;

        }
        else if (di->tm_muxing_spec.type == GANG_EVENT_TRIG_MUXING) {

            // Earliest start time = PREVIOUS period boundary before 'now'. 

            s_time_t z = now / GANG_FINEST_TIME_GRAIN;
            tkt->earliest_start_time = z * GANG_FINEST_TIME_GRAIN;  

            tkt->deadline = 
                tkt->earliest_start_time + 
                di->tm_muxing_spec.params.et_muxing_params.period;

            if (tkt->remaining_time < MARGIN) {
                tkt->remaining_time =
                    di->tm_muxing_spec.params.et_muxing_params.active_time;
            }

        }
        else if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
            // TODO: Revise time logic here.
            
            s_time_t adj_t; 
            s_time_t k;  

            adj_t = now + GANG_FINEST_TIME_GRAIN; 
            k = adj_t / __period_4_be_doms;
            if (adj_t % __period_4_be_doms) {
                k++;    
            }        
            
            tkt->earliest_start_time = k * __period_4_be_doms;

            tkt->deadline = tkt->earliest_start_time + __period_4_be_doms;

            if (tkt->remaining_time < MARGIN) {
                tkt->remaining_time = __quantum_4_be_doms;
            }

        }
        else {
            GANG_LOGT("ERROR: Domain %d with invalid time-multiplexing policy.\n", 
                      dom_id);
            BUG();
        }

        __insert_into_activation_Q(s, tkt);
*/
    }
    else if (reason == GLOBAL_ADJUST) {
        // TODO: Once settled, move here the logic that adjusts time values in tickets
        // related to global adjustment (i.e., adaptation)

        __PANIC("GLOBAL_ADJUST is currently not supported "
                "as a reason for updating time values of a ticket!.\n");
    }
    else {
        __PANIC("ERROR: Invalid reason for updating time values of a ticket."); 
    } 
}

/**
 * Updates time values and other variables in the scheduling tickets currently
 * being used for each multiplexing group, and zeroes the array of current
 * tickets for the multiplexing groups.
 *
 * @param s 
 * @param now 
 */
static SUPPRESS_NOT_USED_WARN 
void __update_current_tickets(sched_info_t* s, s_time_t now) {
    
    // TODO: Revise this function.
    // Are the other reasons for scheduling really needed?

    // TODO: Do we need to check this (vcpu_runnable(tkt->vcpu))?

    cpumask_t checked_cpus;

    cpumask_clear(&checked_cpus);
    for (size_t cpu = 0; cpu < NR_CPUS; cpu++) {
        if (!cpumask_test_cpu(cpu, &checked_cpus)) {
            sched_ticket_t* tkt = s->cur_ticket_per_cpu[cpu];
            if (tkt != NULL) {
                gang_dom_info_t* dom_info = tkt->dom_info; 


                //struct vcpu* vcpu = tkt->vcpu;
                //int reason = (atomic_read(&vcpu->domain->pause_count) == 0) ?
                //             NORMAL_SCHEDULING : PAUSED_DOMAIN;

                //int reason = (test_bit(GSBIT_IS_SLEEPING, &tkt->flags)) ?
                //             PAUSED_DOMAIN : NORMAL_SCHEDULING;

                int reason = NORMAL_SCHEDULING;
                __update_times_in_ticket(s, tkt, now, reason);
                tkt->on_my_behalf = NULL;        

                cpumask_or(&checked_cpus, &checked_cpus, &(dom_info->cpumask));
            }
        }
    }

    memset(s->cur_ticket_per_cpu, 0, (NR_CPUS * sizeof(sched_info_t*))); 


//    // TODO: Optimize as number of muxing groups is known.
//    for (size_t i = 0; i < NR_CPUS; ++i) {
//        sched_ticket_t* tkt = s->cur_ticket_per_muxgroup[i];
//        if (tkt != NULL) {
//
//            //struct vcpu* vcpu = tkt->vcpu;
//            //int reason = (atomic_read(&vcpu->domain->pause_count) == 0) ?
//            //             NORMAL_SCHEDULING : PAUSED_DOMAIN;
//
//            //int reason = (test_bit(GSBIT_IS_SLEEPING, &tkt->flags)) ?
//            //             PAUSED_DOMAIN : NORMAL_SCHEDULING;
//
//            int reason = NORMAL_SCHEDULING;
//            __update_times_in_ticket(s, tkt, now, reason);
//
//            tkt->on_my_behalf = NULL;        
//        }
//    }
// 
//    memset(s->cur_ticket_per_muxgroup, 0, (NR_CPUS * sizeof(sched_info_t*))); 
}

/**
 * Updates time values in current scheduling tickets, moves them to the
 * activation queue, and then moves the tickets whose activation time has
 * arrived from the activation queue to the runnable queue.
 * @param s the local scheduler.
 * @param now the current time.
 * @return the earliest start time of the head ticket in the activation queue.
 */
static
s_time_t __update_sched_info(sched_info_t* s, s_time_t now) {

    // TODO: Revise this function.
    // TODO: Check the need of tkt->flags (GSBIT_XXXX) (defined in this file).

    sched_ticket_t* tkt;
    sched_ticket_t* rmvd_tkt;
    gang_dom_info_t* di;
    //domid_t domid; 

    __update_current_tickets(s, now);

    if (now == INFINITY) {
        return INFINITY;
    }
 
    // Update the EDF runnable queue with tickets from the activation queue
    // whose activation time has passed.
    while (((tkt = __head_of_activation_Q(s)) != NULL) &&
           (tkt->earliest_start_time <= now)) {

        // TODO: Do we need a time margin here?
        // We could add a little margin to compensate for some CPUs arriving
        // here with little difference in times. 

        rmvd_tkt = __remove_from_activation_Q(s, tkt);
        BUG_ON(rmvd_tkt != tkt);

        if (test_bit(GSBIT_IS_SLEEPING, &tkt->flags)) {
            // TODO: Make sure the ticket is inserted into the right collection.
    
            // // clear_bit(GSBIT_IS_SLEEPING, &tkt->flags);
            //     
            // domid = __get_domid_from_dom_info(tkt->dom_info);
            // if (__search_in_waiting_4_event_set(s, domid) == NULL) {
            //     // The ticket is NOT in the waiting-for-event set.
            //     if (__insert_into_waiting_4_event_set(s, tkt)) {
            //         __PANIC("Couldn't insert ticket in the "
            //                 "waiting-for-event set!"); 
            //     }
            // }

            // // set_bit(GSBIT_WAS_WAITING_FOR_EVENT, &tkt->flags);
            
            // continue;
        }        

        if (test_bit(GSBIT_WAS_WAITING_FOR_EVENT, &tkt->flags)) {
            // TODO: Make sure the ticket is inserted into the right collection.

            // VCPU just woke up and is coming out the waiting-for-event set.
            clear_bit(GSBIT_WAS_WAITING_FOR_EVENT, &tkt->flags);
            di = tkt->dom_info; 
            //if (di->tm_muxing_spec.type != GANG_NO_MUXING) {
            //    // Note that the ticket is inserted back in the activation queue
            //    // after updating its time variables. 
            //    __update_times_in_ticket(s, tkt, now, UNPAUSED_DOMAIN); 
            //    continue;
            //}
        }

        __insert_into_runnable_Q(s, tkt);
    }

    if (!__is_activation_Q_empty(s)) {
        sched_ticket_t* ht = __head_of_activation_Q(s);
        return ht->earliest_start_time;
    }
    else {
        return INFINITY;
    }
}


/**
 * Picks a substitute ticket from the runnable and activation queues. 
 */
static SUPPRESS_NOT_USED_WARN
sched_ticket_t* __get_substitute_ticket(sched_info_t* s) {
    // Tickets of non-multiplexed domains (for obvious reasons) and
    // time-triggered domains are skipped. 

    sched_ticket_t* tkt = NULL;
    gang_dom_info_t* di = NULL; 
    bool_t fill_space = FALSE;

    RB_FOREACH(tkt, edf_queue, &s->edf_runnable_Q) {
        fill_space = FALSE;
        di = tkt->dom_info; 
        if (di->tm_muxing_spec.type == GANG_TIME_TRIG_MUXING) {
            fill_space = 
                (di->tm_muxing_spec.params.tt_muxing_params.space_filling != 0);
        }
        else if (di->tm_muxing_spec.type == GANG_EVENT_TRIG_MUXING) {
            fill_space = 
                (di->tm_muxing_spec.params.et_muxing_params.space_filling != 0);
        }
        else if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
            fill_space = 
                (di->tm_muxing_spec.params.be_muxing_params.space_filling != 0);
        }

        if (fill_space && 
            test_bit(GSBIT_SINGLE_VCPU_YIELD, &(tkt->flags)) && 
            test_bit(_VPF_blocked, &(tkt->vcpu->pause_flags))) {
            break;
        } 
    }

    if (tkt == NULL) {
        RB_FOREACH(tkt, actv_queue, &s->activation_Q) {
            fill_space = FALSE;
            di = tkt->dom_info; 
            if (di->tm_muxing_spec.type == GANG_TIME_TRIG_MUXING) {
                fill_space = 
                    (di->tm_muxing_spec.params.tt_muxing_params.space_filling != 0);
            }
            else if (di->tm_muxing_spec.type == GANG_EVENT_TRIG_MUXING) {
                fill_space = 
                    (di->tm_muxing_spec.params.et_muxing_params.space_filling != 0);
            }
            else if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
                fill_space = 
                    (di->tm_muxing_spec.params.be_muxing_params.space_filling != 0);
            }

            if (fill_space && 
                test_bit(GSBIT_SINGLE_VCPU_YIELD, &(tkt->flags)) && 
                test_bit(_VPF_blocked, &(tkt->vcpu->pause_flags))) {
                break;
            } 
        }
    }

    return tkt;
}

/**
 * Main scheduling function that determines which domain to run next. 
 * 
 * @param ops Pointer to this instance of the scheduler structure
 * @param now Current time
 * 
 * @return Address of the VCPU structure scheduled to be run next.
 *         Amount of time to execute the returned VCPU.
 *         Flag for whether the VCPU was migrated.
 */
static struct task_slice 
gang_do_schedule(const struct scheduler* ops, s_time_t now, 
                 bool_t tasklet_work_scheduled) {

    // TODO: Do we need to check whether or not 'current' is runnable? 
    // if (vcpu_runnable(current)) { }

    // TODO: Do we need to check whether or not 'current' is idle? 
    // if (is_idle_vcpu(current)) { }


    const int cpu_id = smp_processor_id();
    sched_info_t* s = LOCAL_SCHED_INFO(cpu_id);

    // Note: Not using muxgroups for the moment.
    //int mux_group_id = cpu_2_muxgroup[cpu_id];

    size_t __num_of_runnable_domains = 0;
    bool_t __non_muxed_domain_present = FALSE;

    struct task_slice ret;
    sched_ticket_t* ticket = NULL;
    s_time_t end_of_time_slice = INFINITY;

    cpumask_t assigned_cpus;

    s_time_t earliest_actv;
    s_time_t tval;

    gang_dom_info_t* dom_info;


    //GANG_LOGT("START: mux_group_id = %d\n", mux_group_id);

    BUG_ON(s == NULL); 

    if (num_of_cohorts == 0 || num_of_muxgroups == 0) {
        // No domains in the gang-scheduled CPU pool. 
        BUG_ON(num_of_cohorts != 0 || num_of_muxgroups != 0); // Sanity check.
        goto EXIT_GANG_DO_SCHEDULE;
    }

    //if (mux_group_id <  0) {
    //    // This CPU belongs to no multiplexing group.  
    //    // Then, it just need to go idle.
    //    goto EXIT_GANG_DO_SCHEDULE;
    //}


    // Update time values in current scheduling tickets, and update activation
    // queue and runnable queue.  Also get the earliest start time of the head
    // ticket in the activation queue.
    earliest_actv = __update_sched_info(s, now);

    if (tasklet_work_scheduled) {
        // This CPU got a tasklet. Then, it just need to go idle.
        goto EXIT_GANG_DO_SCHEDULE;
    }

    cpumask_clear(&assigned_cpus);
    
    while (!__is_runnable_Q_empty(s)) {
 
        int vcpu_id;

        ticket = __remove_from_runnable_Q(s, __head_of_runnable_Q(s));
        dom_info = ticket->dom_info; 

        vcpu_id = (ticket->vcpu != NULL) ? ticket->vcpu->vcpu_id : -1;

        //GANG_LOGT("Ticket from 'Runnable Queue' "
        //          "(domain %d on cpu %d (vcpu %d)). EST = %ld\n", 
        //          __get_domid_from_dom_info(dom_info), cpu_id,
        //          vcpu_id, ticket->earliest_start_time);

        __num_of_runnable_domains++;


        // For each ticket in the EDF runnable queue:
        //     Get the ticket's domain
        //     If there is no overlap between the domain's CPUs and already
        //     assigned CPUs.
        //         Set the domains's CPUs as assigned
        //         For each CPU 'i' of the domain
        //              Set s->cur_ticket_per_cpu[i] = ticket
        //         end of time slice = MIN(end of time slice, 
        //                                 this ticket's activation end)
        if (!cpumask_intersects(&assigned_cpus, &(dom_info->cpumask))) {

            //GANG_LOGT("No intersection\n"); 

            cpumask_or(&assigned_cpus, &assigned_cpus, &(dom_info->cpumask));

            for (int i = 0; i < NR_CPUS; i++) {
                if (cpumask_test_cpu(i, &(dom_info->cpumask))) {
                    BUG_ON(s->cur_ticket_per_cpu[i] != NULL);
                    s->cur_ticket_per_cpu[i] = ticket;
                    //GANG_LOGT("Set s->cur_ticket_per_cpu[%d]\n", i); 
                }
            }

            ticket->activated_at = now;

            tval = (dom_info->tm_muxing_spec.type == GANG_NO_MUXING) ? 
                    INFINITY : (now + ticket->remaining_time);

            end_of_time_slice = MIN(end_of_time_slice, tval);
            //GANG_LOGT("Ticket Time Remaining: %ld, End of Timeslice: %ld\n",
            //          ticket->remaining_time, end_of_time_slice);

            // Sanity checks
            if (dom_info->tm_muxing_spec.type == GANG_NO_MUXING) {
                __non_muxed_domain_present = TRUE;

                if (ticket->remaining_time != INFINITY) {
                    GANG_LOGT("ERROR: Non-multiplexed domain %d "
                              "with ticket->remaining_time != INFINITY\n", 
                              __get_domid_from_dom_info(dom_info));
                    BUG();
                }
            }
            else {
                if (ticket->remaining_time == INFINITY) {
                    GANG_LOGT("ERROR: Multiplexed domain %d "
                              "with ticket->remaining_time == INFINITY\n", 
                              __get_domid_from_dom_info(dom_info));
                    BUG();
                }

                if (ticket->remaining_time < MARGIN) {
                    GANG_LOGT("ERROR: Multiplexed domain %d "
                              "with ticket->remaining_time %ld < MARGIN = %ld \n",
                              __get_domid_from_dom_info(dom_info),
                              ticket->remaining_time,
                              MARGIN);
                    BUG();
                }
            }
        }
        else {
            //GANG_LOGT("Intersection\n"); 

            tval = ticket->deadline - ticket->remaining_time;        
            BUG_ON(tval <= 0);

            if (tval <= now) {
                GANG_LOGT("Potential end of time slice (%ld) <= now (%ld).\n",
                          tval, now);
                tval = now + ms_2_ns(1); 
            }
            end_of_time_slice = MIN(end_of_time_slice, tval);

            // NOTE: After this point the head ticket of the activation queue
            // may have an earliest_start_time <= now. The reason is that here
            // the skipped ticket is put back into the activation queue.
            __insert_into_activation_Q(s, ticket);
        }

        if (cpumask_equal(&assigned_cpus, &cohorts[cpu_id])) {
            // Once a ticket is assigned to each CPU in the cohort
            // (assigned_cpus == cohort[cpu_id]), the scheduler is
            // done.
            break;
        }
    }

    // Sanity check
    if (__non_muxed_domain_present && __num_of_runnable_domains > 1) {
       __PANIC("Non-multiplexed domain is NOT alone.\n");
    }

    // Get the ticket for the local CPU. 
    ticket = s->cur_ticket_per_cpu[cpu_id];


    // The time slice should end at MIN(end_of_time_slice, earliest_actv).
    end_of_time_slice = MIN(end_of_time_slice, earliest_actv); 
    if (end_of_time_slice < INFINITY) {
        end_of_time_slice = (end_of_time_slice / GANG_FINEST_TIME_GRAIN) * GANG_FINEST_TIME_GRAIN;
    }


    //GANG_LOGT("End of Timeslice after earliest_actv check: %ld\n", end_of_time_slice);

 
    // Handle guest scheduler commands SCHEDOP_block, SCHEDOP_poll,
    // SCHEDOP_yield.
    if (!is_idle_vcpu(current)) {
        // If the selected ticket's VCPU was the current VCPU and it is singly
        // yielding or blocking, then we select a substitute VCPU to run on its
        // behalf.
    
        // GANG_NO_MUXING domains ignore GSBIT_SINGLE_VCPU_YIELD and
        // _VPF_blocked.
        // GANG_TIME_TRIG_MUXING domains ignore _VPF_blocked.

        sched_ticket_t* cur_tkt = SCHED_TICKET(current);
        gang_dom_info_t* cur_di = cur_tkt->dom_info;

        if (ticket == cur_tkt) {
            if (cur_di->tm_muxing_spec.type != GANG_NO_MUXING &&
                test_bit(GSBIT_SINGLE_VCPU_YIELD, &(cur_tkt->flags))) {
                int vcpu_id = -1;
                if (cur_tkt->vcpu != NULL) {
                    vcpu_id = cur_tkt->vcpu->vcpu_id;
                }
                GANG_LOG("VCPU (%d) of domain (%d) yielded, getting substitute.\n",
                         vcpu_id, cur_di->domain->domain_id);

                ticket->on_my_behalf = NULL; // __get_substitute_ticket(s);

                if (ticket->on_my_behalf == NULL) {
                    // No substitute available, then force use of idle VCPU.
                    ticket = NULL;
                }
            }

            // FIXME: What to do when _VPF_blocked is on? 
            //        It does not seem to matter so far.

            //else if (cur_di->tm_muxing_spec.type != GANG_NO_MUXING &&
            //         cur_di->tm_muxing_spec.type != GANG_TIME_TRIG_MUXING &&
            //         test_bit(_VPF_blocked, &(current->pause_flags))) {
            //    GANG_LOG("VCPU (%d) of domain (%d) is paused, getting substitute.\n",
            //             cur_tkt->vcpu->vcpu_id, cur_di->domain->domain_id);
            //    ticket->on_my_behalf = __get_substitute_ticket(s);
            //}
        }

        // Clear 'single VCPU YIELD' flag before scheduling out.
        clear_bit(GSBIT_SINGLE_VCPU_YIELD, &(cur_tkt->flags));
    }

EXIT_GANG_DO_SCHEDULE:


    if (ticket != NULL) {
        //GANG_LOGT("ticket != NULL\n");

        // Sanity checks. The ticket for the local CPU must have a valid VCPU.
        BUG_ON(ticket->vcpu == NULL);
        BUG_ON(is_idle_vcpu(ticket->vcpu));

        ret.task = ticket->vcpu;

        // FIXME: Enable space filling feature: ticket->on_my_behalf 
        //if (ticket->vcpu == NULL) {
        //    ticket->on_my_behalf = NULL; // __get_substitute_ticket(s);
        //    if (ticket->on_my_behalf == NULL) {
        //        ret.task = idle_vcpu[cpu_id];
        //    }
        //    else {
        //        ret.task = ticket->on_my_behalf->vcpu;
        //    }
        //} 
        //else {
        //    if (ticket->on_my_behalf == NULL) {
        //        ret.task = ticket->vcpu;
        //    }
        //    else {
        //        ret.task = ticket->on_my_behalf->vcpu;
        //    }
        //}
    }
    else {  
        //GANG_LOGT("ticket == NULL\n");
        ret.task = idle_vcpu[cpu_id];
    }

    if (end_of_time_slice < INFINITY) {
        //GANG_LOGT("end_of_time_slice = %ld us\n", end_of_time_slice);
        ret.time = end_of_time_slice - now; // get_s_time(); 
        if (ret.time < 0) {
            ret.time = 0;
        }
    }
    else {
        //GANG_LOGT("end_of_time_slice = INFINITY\n");
        ret.time = -1; // A negative value means 'no limit' to Xen.
    }

    ret.migrated = FALSE; 

    //GANG_LOGT("Activating domid: %d, vcpu: %d, end_of_time_slice: %ld (%ld)\n",
    //          ret.task->domain->domain_id, 
    //          ((is_idle_vcpu(ret.task)) ? -1 : ret.task->vcpu_id), 
    //          end_of_time_slice,
    //          ret.time);

    return ret;
}

static void 
gang_yield(const struct scheduler* ops, struct vcpu* vcpu) {

    sched_ticket_t * const tkt = SCHED_TICKET(vcpu);

    // Let the scheduler know that this (single) VCPU is trying to yield.
    set_bit(GSBIT_SINGLE_VCPU_YIELD, &tkt->flags);
}


SUPPRESS_NOT_USED_WARN
static void 
gang_wake(const struct scheduler* ops, struct vcpu* vcpu) {
    // TODO: Do we really need to implement this?
    // For PV guest domains, it seems we are fine without it.
    // How about HVM domains? Need to test.
}

SUPPRESS_NOT_USED_WARN
static void 
gang_sleep(const struct scheduler* ops, struct vcpu* vcpu) {
    // TODO: Same comment as in gang_wake()
}

///////////////////////////////////////////////////////////////////////////////
// Auxiliary functions related to gang scheduling policies.
///////////////////////////////////////////////////////////////////////////////

/**
 * Validates the parameters of a time-triggering policy. 
 */
static 
bool_t __validate_tt_muxing_params(tt_muxing_params_t* params) {

    s_time_t period = (s_time_t) params->period;
    s_time_t active_time = (s_time_t) params->active_time; 

    if (period < GANG_FINEST_TIME_GRAIN_IN_US) {
        GANG_LOGT("period = %ld us < GANG_FINEST_TIME_GRAIN_IN_US = %d us\n",
                  period, GANG_FINEST_TIME_GRAIN_IN_US);
        return FALSE;
    }

    if (period >= INFINITY) {
        GANG_LOGT("period = %ld == INFINITY\n", period);
        return FALSE;
    }

    if (active_time < GANG_FINEST_TIME_GRAIN_IN_US) {
        GANG_LOGT("active time = %ld us < GANG_FINEST_TIME_GRAIN_IN_US = %d us\n",
                  active_time, GANG_FINEST_TIME_GRAIN_IN_US);
        return FALSE;
    }   

    if (active_time >= INFINITY) {
        GANG_LOGT("active_time = %ld == INFINITY\n", active_time);
        return FALSE;
    }

    if (period < active_time)  {
        GANG_LOGT("period = %ld us < active_time = %ld us\n", period, active_time);
        return FALSE;
    }   

    return TRUE;
}

/**
 * Validates the parameters of a event-triggering policy. 
 */
static 
bool_t __validate_et_muxing_params(et_muxing_params_t* params) {

    s_time_t period = (s_time_t) params->period;
    s_time_t active_time = (s_time_t) params->active_time; 

    if (period < GANG_FINEST_TIME_GRAIN_IN_US) {
        GANG_LOGT("period = %ld us < GANG_FINEST_TIME_GRAIN_IN_US = %d us\n",
                  period, GANG_FINEST_TIME_GRAIN_IN_US);
        return FALSE;
    }   

    if (period >= INFINITY) {
        GANG_LOGT("period = %ld == INFINITY\n", period);
        return FALSE;
    }

    if (active_time < GANG_FINEST_TIME_GRAIN_IN_US) {
        GANG_LOGT("active time = %ld us < GANG_FINEST_TIME_GRAIN_IN_US = %d us\n",
                  active_time, GANG_FINEST_TIME_GRAIN_IN_US);
        return FALSE;
    }   

    if (active_time >= INFINITY) {
        GANG_LOGT("active_time = %ld == INFINITY\n", active_time);
        return FALSE;
    }

    if (period < active_time)  {
        GANG_LOGT("period = %ld us < active_time = %ld us\n", period, active_time);
        return FALSE;
    }   

    return TRUE;
}

/**
 * Validates the parameters of a best-effort policy. 
 */
inline static 
bool_t __validate_be_muxing_params(be_muxing_params_t* params) {
    // So far, nothing to validate. 
    return TRUE;
}

/**
 * Validates the parameters of the time multiplexing policy.
 */
static 
bool_t __validate_gang_sched_policy(gang_sched_policy_t* p) {

    bool_t v = TRUE;

    if (p == NULL) {
        return FALSE;
    }

    switch (p->type) {
        case GANG_NO_MUXING:
            // nothing to validate
            break;
        case GANG_TIME_TRIG_MUXING:
            v = __validate_tt_muxing_params(&p->params.tt_muxing_params);
            break;
        case GANG_EVENT_TRIG_MUXING:
            v = __validate_et_muxing_params(&p->params.et_muxing_params);
            break;
        case GANG_BEST_EFFORT_MUXING:
            v = __validate_be_muxing_params(&p->params.be_muxing_params);
            break;
        default:
            v = FALSE;
            break;
    }

    return v;
}

///////////////////////////////////////////////////////////////////////////////
// Auxiliary functions and variables for 'gang_adjust_global(...)'.
//
// The gang_adjust_global(...) function is central to adaptation (i.e.,
// modifications on CPUs and scheduling parameters assigned to gang-scheduled
// domains). 
///////////////////////////////////////////////////////////////////////////////

/**
 * Copies the current parameters of the domains, except cohort that is set to -1
 * and muxgroup mask that is cleared. 
 * @param[in,out] infos array populated by this function. 
 *                      It must be provided by the caller. 
 * @param[in] len length of the infos array.
 * @param[in] cpu_pool CPU pool controlled by the gang scheduler.
 * @return number of current domains.
 */
SUPPRESS_NOT_USED_WARN
static int 
__get_all_domain_infos(gang_dom_info_t* infos, uint16_t len, 
                       struct cpupool* cpu_pool) {
    struct domain* d;
    uint16_t n = 0;

    BUG_ON(len > GANG_SCHED_MAX_DOMAINS); 

    for_each_domain_in_cpupool(d, cpu_pool) {
        if (n < len) {
            gang_dom_info_t* di = DOMAIN_SCHED_INFO(d); 

            infos[n].domain = di->domain;
            cpumask_copy(&(infos[n].cpumask), &di->cpumask); 
            infos[n].tm_muxing_spec = di->tm_muxing_spec;
            infos[n].cohort = -1;
            cpumask_clear(&(infos[n].muxgroup_mask));
        }

        n++;
    }

    return n;
}

/**
 * Updates the domain info items with the passed values.
 * @param infos array of domain info items. 
 * @param len number of domain info items in the array.
 * @param params desired new parameters for the domains. 
 * @return number of updated domain info items, if successful; 
 *         otherwise, a negative number.
 */
SUPPRESS_NOT_USED_WARN
static int 
__update_domain_infos(gang_dom_info_t* infos, uint16_t len,
                      xen_sysctl_gang_schedule_t* params) {

    // TODO: Consider, for each domain, comparing the current and new CPU masks
    // and gang scheduling policy parameters. If they are the same, don't
    // update.
    SUPPRESS_NOT_USED_WARN
    int err = 0;
    int updated_domains = params->num_dom_entries;
    
    if (len < params->num_dom_entries) {
        GANG_LOGT("Trying to update more domains (%d) than "
                  "the ones you are passing (%d). \n",
                  params->num_dom_entries, len);
        return -EINVAL;
    }

    for (int e = 0; e < params->num_dom_entries; e++) {
        cpumask_var_t new_cpumask = NULL;

        gang_sched_dom_conf_t* dom_entry = &params->dom_entries[e];

        gang_dom_info_t* di = NULL;

        for (int j = 0; j < len; j++) {
            gang_dom_info_t* ddii = &infos[j];
            domid_t domid = __get_domid_from_dom_info(ddii);
            if (dom_entry->domid == domid) { 
                di = ddii; 
                break;
            }
        }

        if (di == NULL) {
            GANG_LOGT("Domain in entry %d (domid: %d) is not "
                      "in the array of domains to update.\n",
                      e, dom_entry->domid);
            return -EINVAL;
        }
        
        err = xenctl_bitmap_to_cpumask(&new_cpumask, &dom_entry->cpumap);
        if (!err) {
            cpumask_copy(&di->cpumask, new_cpumask); 
            free_cpumask_var(new_cpumask);
        }
        else {
            return err;
        }
        
        memcpy(&di->tm_muxing_spec, 
               &dom_entry->gang_sched_policy,
               sizeof(gang_sched_policy_t));
    }

    return updated_domains;
}

/**
 * Sorts the domain infos according the expected order for scheduling
 * feasibility analysis and validation.
 */
static void 
__sort_domain_infos(gang_dom_info_t** arr, uint16_t len) {
    // We simply use bubblesort since we don't expect thousands of domains.
    uint16_t i = 0;
    gang_dom_info_t* tmp;

    while (i != len - 1) {
        int32_t item_ord = arr[i]->tm_muxing_spec.type; 
        int32_t next_item_ord = arr[i+1]->tm_muxing_spec.type; 
        if (item_ord > next_item_ord) {
            tmp = arr[i];
            arr[i] = arr[i+1];
            arr[i+1] = tmp;
            i = 0;
        }
        else {
            i++;
        }
    }
}

/**
 * Determines whether or not the given resource allocation to domains is valid.
 * It checks schedulability of the assignment of CPUs to domains.
 * @param arr
 * @param len
 * @return true if valid; otherwise, false.  
 */
SUPPRESS_NOT_USED_WARN
static bool_t 
__validate_resource_allocation(gang_dom_info_t** arr, uint16_t len) {

    __sort_domain_infos(arr, len);
    
    // TODO: Implement!

    return TRUE;
}

/**
 * Adds a domain to the multiplexing groups.
 * This function creates and divides the multiplexing groups as necessary.
 *
 * @param di 
 * @param __cpu_2_muxgroup
 * @param __muxgroups
 * @param __num_of_muxgroups
 */
SUPPRESS_NOT_USED_WARN
static void 
__add_dom_to_muxgroups(gang_dom_info_t* di, 
                       int* __cpu_2_muxgroup, 
                       cpumask_t* __muxgroups, 
                       size_t* __num_of_muxgroups) {
    // FIXME: This function has some bugs. 
    //        Currently we are not using the muxgroups, they are an optimization
    //        anyway.

    // TODO: Can we make it more efficient?
    // TODO: Add log messages to ease degugging.

    int cpu_id;
    int muxgroup_id;
    int c;

    cpumask_t* the_muxgroup;
    cpumask_t* new_muxgroup;
 
    // CPUs assigned to the domain that haven't been processed yet.
    cpumask_t pending_cpus;

    cpumask_clear(&pending_cpus);
    cpumask_copy(&pending_cpus, &di->cpumask);

    for_each_cpu(cpu_id, &di->cpumask) {

        if (!cpumask_test_cpu(cpu_id, &pending_cpus)) {
            continue;
        }
        
        muxgroup_id = __cpu_2_muxgroup[cpu_id];

        if (muxgroup_id >= 0) {
            // The CPU assigned to the domain belongs to an existing mux group.

            the_muxgroup = &__muxgroups[muxgroup_id];

            cpumask_andnot(&pending_cpus, &pending_cpus, the_muxgroup);

            if (cpumask_subset(&di->cpumask, the_muxgroup)) {
                // The CPUs assigned to the domain are all in the mux group.
                // Then, no need to split the mux group into two groups. 
                // Just add the domain to the mux group.

                cpumask_set_cpu(muxgroup_id, &di->muxgroup_mask);
            }
            else {
                // Only some of the CPUs assigned to the domain are in the mux group.
                // Then, we need to split the mux group into two.

                muxgroup_id = *__num_of_muxgroups; // Get a new/unused mux group 
                (*__num_of_muxgroups)++;
                BUG_ON((*__num_of_muxgroups) > NR_CPUS);

                new_muxgroup = &__muxgroups[muxgroup_id];

                cpumask_copy(new_muxgroup, the_muxgroup);
                cpumask_and(new_muxgroup, new_muxgroup, &di->cpumask);

                // Update the '__cpu_2_muxgroup' map
                for_each_cpu(c, new_muxgroup) {
                    __cpu_2_muxgroup[c] = muxgroup_id;
                }
                
                cpumask_set_cpu(muxgroup_id, &di->muxgroup_mask);

                // Retire the CPUs of the existing mux group
                cpumask_andnot(the_muxgroup, the_muxgroup, &di->cpumask);
            }
        }
        else {
            // The CPU assigned to the domain does not belong to any mux group.
            // Then, create a mux group with the all the CPUs required by the
            // domain AND not present in any other mux group.

            muxgroup_id = *__num_of_muxgroups;
            (*__num_of_muxgroups)++;
            BUG_ON(*__num_of_muxgroups > NR_CPUS);

            new_muxgroup = &__muxgroups[muxgroup_id]; // Supposed to be cleared.

            // Store in 'new_muxgrp' the CPU in no mux group.
            for (int i = 0; i < NR_CPUS; i++) {
                if (__cpu_2_muxgroup[i] < 0) {
                    cpumask_set_cpu(i, new_muxgroup);
                }
            }    

            cpumask_and(new_muxgroup, new_muxgroup, &di->cpumask);

            cpumask_set_cpu(muxgroup_id, &di->muxgroup_mask);

            // Update the '__cpu_2_muxgroup' map
            for_each_cpu(c, new_muxgroup) {
                __cpu_2_muxgroup[c] = muxgroup_id;
            }

            cpumask_andnot(&pending_cpus, &pending_cpus, new_muxgroup);

        }
    }
}

/**
 * Identifies and merges cohorts as necessary.
 *
 * @param[in] di  
 * @param[in,out] __cohorts
 * @param[in,out] num_of_cohorts
 */
SUPPRESS_NOT_USED_WARN
static void 
__update_cohorts(gang_dom_info_t* di, 
                 cpumask_t* __cohorts, 
                 size_t* __num_of_cohorts) {

    // TODO: Revise this function. Can we make it more efficient?
    // TODO: Add log messages to ease degugging.

    cpumask_t* the_cohort;
    cpumask_t* new_cohort;
    int cohort_id;
    int i;

    bool_t intersect = FALSE;

    // Does the domain's CPU mask intersect any existing cohort?  
   
    for (size_t n = 0; n < (*__num_of_cohorts); n++) {
        the_cohort = &__cohorts[n];
        if ((intersect = cpumask_intersects(&di->cpumask, the_cohort))) {
            // The domain's CPU mask and the cohort's CPU mask have some common bits.

            if (!cpumask_subset(&di->cpumask, the_cohort)) {
                // But the domain's CPU mask is NOT a subset of the cohort's CPU
                // mask. That is, there are some bits that are NOT common.
                // Then, expand the cohort's reach.  

                cpumask_or(the_cohort, the_cohort, &di->cpumask);
            }

            break;
        }
    }   

    if (!intersect) {
        // No intersection between the the domain's CPU mask and any of the
        // cohorts.  Then, create a new cohort with the CPUs required by the
        // domain.
        
        cohort_id = (*__num_of_cohorts); // Get a new/unused cohort 
        (*__num_of_cohorts)++;

        new_cohort = &__cohorts[cohort_id];
        cpumask_or(new_cohort, new_cohort, &di->cpumask);
    }   

    // There may be overlapping cohorts at this point, if so we must merge them.
    i = 0;
    while(i < ((*__num_of_cohorts)-1)) {

        for (int j = i+1; j < (*__num_of_cohorts); j++) {
            cpumask_t* ca = &__cohorts[i];
            cpumask_t* cb = &__cohorts[j];
            if (cpumask_intersects(ca, cb) ) {
                // Cohorts A and B overlap, then merge them.
                cpumask_or(ca, ca, cb);

                // Fill the hole in the array of cohorts.
                for (int k = j+1; k < (*__num_of_cohorts); k++) {
                    cpumask_t* cx = &__cohorts[k-1];
                    cpumask_t* cy = &__cohorts[k];
                    cpumask_copy(cx, cy);     
                }   

                (*__num_of_cohorts)--;
                
                i = -1; // Restart the outer 'while' loop.
                break;
            }   
        }   

        i++;
    }   
}
   
/**
 * Selects and sets the CPU cohort that corresponds to the given domain.
 * 
 * @param di domain info.
 * @param cohorts array of CPU cohorts, with each element being a CPU mask.
 * @param num_of_cohorts number of CPU cohorts.
 */
SUPPRESS_NOT_USED_WARN
static void 
__set_cohort_in_domain(gang_dom_info_t* di, 
                       cpumask_t* cohorts, 
                       size_t num_of_cohorts) {

    // TODO: Add log messages to ease degugging.

    bool_t found = FALSE;

    for (size_t nc = 0; nc < num_of_cohorts; nc++) {
        cpumask_t* the_cohort = &cohorts[nc];
        if (cpumask_subset(&di->cpumask, the_cohort)) {
            // The domain belongs to the cohort because the domain's CPU mask is
            // a subset of the cohort's CPU.
            di->cohort = nc;
            found = TRUE;
            break;
        }
    }

    BUG_ON(found == FALSE);
}

/**
 * Establishes the cohorts and multiplexing groups for the domains and populates
 * the passed arrays.
 *
 * @param[in] arr Array of domain infos describing domain allocations
 * @param[in] len Length of 'arr'
 * @param[out] __cohorts
 * @param[out] __cpu_2_cohort
 * @param[out] __be_doms_in_cohort
 * @param[out] __num_of_cohorts
 * @param[out] __muxgroups 
 * @param[out] __cpu_2_muxgroup
 * @param[out] __num_of_muxgroups
 */
SUPPRESS_NOT_USED_WARN
static void 
__populate_cohorts_and_muxgroups(gang_dom_info_t** arr, 
                                 uint16_t len,
                                 cpumask_t* __cohorts,
                                 int* __cpu_2_cohort, 
                                 int* __be_doms_in_cohort,
                                 size_t* __num_of_cohorts,
                                 cpumask_t* __muxgroups,
                                 int* __cpu_2_muxgroup,
                                 size_t* __num_of_muxgroups) {
    // TODO: Revise this function. Can we make it more efficient?

    // Initialize cohort and muxgroup counts to zero.
    *__num_of_cohorts = 0;
    *__num_of_muxgroups = 0;

    GANG_LOGT("\n");
 
    // Initialize __cpu_2_cohort and __cpu_2_muxgroup map arrays.
    for (int j = 0; j < NR_CPUS; j++) {
        __cpu_2_cohort[j] = -1;
        __cpu_2_muxgroup[j] = -1;
    }

    // Initialize counts of best-effort domains per cohort.
    memset(__be_doms_in_cohort, 0, sizeof(int) * NR_CPUS);


    for (uint16_t i = 0; i < len; i++) {
        gang_dom_info_t* di = arr[i];
        __add_dom_to_muxgroups(di, 
                               __cpu_2_muxgroup, 
                               __muxgroups,
                               __num_of_muxgroups);
        __update_cohorts(di, __cohorts, __num_of_cohorts);
    }

    BUG_ON(*__num_of_muxgroups == 0);
    BUG_ON(*__num_of_muxgroups > NR_CPUS);
    BUG_ON(*__num_of_cohorts == 0);
    BUG_ON(*__num_of_cohorts > NR_CPUS);

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

}

/**
 * Returns the number of VCPUs in the given domain.
 */
static 
int __count_vcpus_in_domain(struct domain* d) {   
    int count = 0;
    struct vcpu* v;
    
    for_each_vcpu(d, v) {
        count++;
    }
    return count;
}

/**
 * Sets the affinity of VCPUs of the given domain to its assigned CPUs.
 * @param dom_info
 */
static
void __setup_vcpus_for_domain(gang_dom_info_t* di) {
    
    struct domain* d;

    int cpus_in_pool; // Number of CPUs in the CPU pool.
    int assigned_cpus_2_dom; // Number of CPUs assigned to the domain.

    int vcpus_in_dom; // Number of VCPUs in the domain.
    struct vcpu* v;

    int cpuid;

    d = di->domain;

    BUG_ON(d == NULL);
    GANG_LOGT("domid = %d\n", d->domain_id);

    BUG_ON(d->cpupool == NULL);

    cpus_in_pool = cpumask_weight(d->cpupool->cpu_valid);
    assigned_cpus_2_dom = cpumask_weight(&di->cpumask);
    vcpus_in_dom = __count_vcpus_in_domain(d);

    GANG_LOGT("domid = %d, assigned CPUs = %d, VCPUs = %d, Max VCPUs = %d\n",
              d->domain_id, assigned_cpus_2_dom, vcpus_in_dom, d->max_vcpus);

    // Caller function ensures that:  
    // (VCPUs in domain) == (Domain's max VCPUs) <= (CPUs in the pool)
    BUG_ON(vcpus_in_dom != d->max_vcpus);
    BUG_ON(vcpus_in_dom > cpus_in_pool);

    BUG_ON(assigned_cpus_2_dom != vcpus_in_dom);


    // The code below is similar to that in function sched_move_domain(...) in
    // xen/common/schedule.c (line 278). 

    cpuid = cpumask_first(&di->cpumask); 
    for_each_vcpu(d, v) {
        spinlock_t* lock;
        BUG_ON(cpuid == NR_CPUS);
        BUG_ON(!cpu_online(cpuid));

        //if (v->processor != cpuid) {       

            migrate_timer(&v->periodic_timer, cpuid);
            migrate_timer(&v->singleshot_timer, cpuid);
            migrate_timer(&v->poll_timer, cpuid);

            cpumask_clear(v->cpu_affinity);
            cpumask_set_cpu(cpuid, v->cpu_affinity);

            lock = vcpu_schedule_lock_irq(v);
            v->processor = cpuid;
            // With v->processor modified we must not
            // - make any further changes assuming we hold the scheduler lock,
            // - use vcpu_schedule_unlock_irq().
            spin_unlock_irq(lock);

            if (!d->is_dying) {
                evtchn_move_pirqs(v);
            }
        //}

        GANG_LOGT("VCPU %d in dom %d gets CPU %d\n", 
                  v->vcpu_id, d->domain_id, v->processor);

        cpuid = cpumask_next(cpuid, &di->cpumask);

        // Note that v->sched_priv is set and the scheduler is called later.
    }

    GANG_LOGT("Affinitized VCPUs of dom %d!\n", d->domain_id);
}

/**
 * Updates the local scheduler by wiping out all the tickets and re-initializing
 * the local scheduler from scratch.
 *
 * This is chosen as an *initial* version because it is simple.
 * An obvious disadvantage is that we lose the time usage information of
 * domains. Thus, we don't claim that this is the correct way to proceed. 
 * 
 * This function can update the local scheduler because the global arrays have
 * been updated before it gets called.
 */
SUPPRESS_NOT_USED_WARN
static void 
__update_local_sched_v0(gang_dom_info_t** arr, 
                        uint16_t len, 
                        s_time_t now,
                        unsigned int designated_cpu_id) {

    gang_dom_info_t* di;
    domid_t domid;

    bool_t dom_got_this_cpu;

    struct vcpu* vcpu;
    bool_t vcpu_found;

    sched_ticket_t* tkt; 
    sched_ticket_t* tmp_tkt;

    unsigned int cpu_id = smp_processor_id();
    sched_info_t* si = LOCAL_SCHED_INFO(cpu_id); 

    int cpu_cohort = cpu_2_cohort[cpu_id];

    int be_doms_counter = 0;

    bool_t res; 

    // The expected time at which global adjustment will be all done
    // and scheduling activities will resume.
    s_time_t will_resume_at = 
        (((now + __adj_time_upper_bound) / GANG_FINEST_TIME_GRAIN) + 1) * GANG_FINEST_TIME_GRAIN;

    // Variables for sanity checks 
    bool_t muxed_dom_in_cohort = FALSE;
    bool_t non_muxed_dom_in_cohort = FALSE;
    domid_t non_muxed_domid = -1;

    //GANG_LOGT("will_resume_at = %ld us\n", will_resume_at);
    //GANG_LOGT("Updating schedule with %d domain infos\n", len);

    // Reset the local schedule. 
    // This involves deletion of all the scheduling tickets and replenish the
    // remaining time per mux group for best-effort domains.
    __deinit_sched_info(si, FALSE);

    // For each domain.
    for (uint16_t l = 0; l < len; l++) {
        SUPPRESS_NOT_USED_WARN int vcpu_id = -1;
        di = arr[l];
        BUG_ON(di->domain->cpupool == NULL);

        // Only the designated CPU updates the domain's private scheduling info.
        // Note that no barrier is needed because we only update the domain's
        // info, but that info is not use until the entire 'adjustment' process
        // (CPU reassignment) finishes. 
        if (cpu_id == designated_cpu_id) {
            gang_dom_info_t* dom_info = DOMAIN_SCHED_INFO(di->domain);
            cpumask_copy(&dom_info->cpumask, &di->cpumask);
            memcpy(&dom_info->tm_muxing_spec, &di->tm_muxing_spec,
                   sizeof(gang_sched_policy_t));
            dom_info->cohort = di->cohort;
            cpumask_copy(&dom_info->muxgroup_mask, &di->muxgroup_mask);
        }

        if (cpu_cohort != di->cohort) {
            // The domain is not in this cohort. Then the local scheduler does
            // not need to know about it.
            continue;
        }

        // Find the domain's VCPU for this CPU.
        // Note that each domain's VCPU-to-CPU map(i.e., vcpu->processor of each
        // domain's VCPU) must be updated before calling this function.
        vcpu = NULL;
        vcpu_found = FALSE;
        for_each_vcpu(di->domain, vcpu) {
            if (cpu_id == vcpu->processor) {
                vcpu_found = TRUE;
                break;
            }
        }                    

        dom_got_this_cpu = cpumask_test_cpu(cpu_id, &di->cpumask);

        GANG_LOGT("dom_got_this_cpu = %s, vcpu_found = %s\n",
                  dom_got_this_cpu ? "TRUE" : "FALSE",
                  vcpu_found ? "TRUE" : "FALSE");

        // Use ((!A) != (!B)) to handle different integer values meaning TRUE.
        BUG_ON((!dom_got_this_cpu) != (!vcpu_found));

        if (vcpu_found) {
            BUG_ON(vcpu == NULL);
            BUG_ON(is_idle_vcpu(vcpu));
        }

        // Allocate and initialize (with default values) the scheduling ticket
        tkt = (sched_ticket_t*) gang_alloc_vdata(di->domain->cpupool->sched,
                                                 (dom_got_this_cpu ? vcpu : NULL),
                                                 DOMAIN_SCHED_INFO(di->domain));

        //GANG_LOGT("Allocated a ticket @ 0x%p for domain #%d (vcpu_found = %s)\n", 
        //          (void*) tkt, 
        //          di->domain->domain_id, 
        //          (vcpu_found ? "TRUE":"FALSE"));
        BUG_ON(tkt == NULL);


        if (dom_got_this_cpu) {
            vcpu->sched_priv = tkt; 
            vcpu_id = vcpu->vcpu_id;
        }

        // TODO: Do we really need to insert the ticket in waiting for event
        // set? Note that few lines below we remove the ticket from the set.
        // Maybe when we move the code below to __update_times_in_ticket(...)

        //GANG_LOGT("Before inserting ticket @ 0x%p for domain #%d (vcpu_found = %s)\n", 
        //          (void*) tkt, di->domain->domain_id, (vcpu_found ? "TRUE":"FALSE"));
        res = __insert_into_waiting_4_event_set(si, tkt);

        //GANG_LOGT("Inserted ticket into event set. (vcpu_found = %s)\n",
        //          (vcpu_found ? "TRUE":"FALSE"));
        BUG_ON(res == FALSE);

        GANG_LOGT("Updating Domain %d: cpumask=%lx, policy=%d, vcpu=%d\n",
                  di->domain->domain_id, 
                  di->cpumask.bits[0],
                  di->tm_muxing_spec.type, 
                  vcpu_id);

        domid = __get_domid_from_dom_info(di);



        // TODO: Here we should call __update_times_in_ticket(...) with
        // GLOBAL_ADJUST as the calling reason, but for the moment we don't.
        // We'll do this once the logic for adjusting tickets' time values is
        // settled. 
        
        // Sanity check. If there an always-active domain,
        // that's the only domain that must be in the cohort.
        if (di->tm_muxing_spec.type == GANG_NO_MUXING) {
            non_muxed_dom_in_cohort = TRUE;
            non_muxed_domid = domid;
        }
        else {
            muxed_dom_in_cohort = TRUE;
        }

        if (non_muxed_dom_in_cohort && muxed_dom_in_cohort) {
            GANG_LOGT("ERROR: Domain %d is a Non-Multiplexed Domain, "
                      "but other domains are also in the cohort %d.\n",
                      non_muxed_domid, cpu_cohort);
            BUG_ON(non_muxed_dom_in_cohort && muxed_dom_in_cohort);
        }

 
        //GANG_LOGT("Getting ticket from 'Waiting Set' (domain %d on cpu %d (vcpu %d))\n",
        //          domid, cpu_id, vcpu_id);
        tmp_tkt = __remove_from_waiting_4_event_set(si, domid);
        BUG_ON(tkt != tmp_tkt);

        if (di->tm_muxing_spec.type == GANG_NO_MUXING) {
            tkt->remaining_time = INFINITY;
            tkt->earliest_start_time =
                 MAX(di->tm_muxing_spec.params.no_muxing_params.from,
                     will_resume_at);
            tkt->deadline = INFINITY;

            GANG_LOGT("Inserting No Mux ticket to 'Activation Queue' "
                      "(domain %d on cpu %d (vcpu %d)). EST = %ld\n", 
                      domid, cpu_id, vcpu_id, tkt->earliest_start_time);
            __insert_into_activation_Q(si, tkt);

        }
        else if (di->tm_muxing_spec.type == GANG_TIME_TRIG_MUXING) {
            tkt->remaining_time =
                 di->tm_muxing_spec.params.tt_muxing_params.active_time;
            tkt->earliest_start_time =
                 __adjust_activation_time(will_resume_at,
                                          di->tm_muxing_spec.params.tt_muxing_params.from,
                                          di->tm_muxing_spec.params.tt_muxing_params.period);
            tkt->deadline = tkt->earliest_start_time +
                            di->tm_muxing_spec.params.tt_muxing_params.period;

            GANG_LOGT("Inserting time-triggered ticket to 'Activation Queue' "
                      "(domain %d on cpu %d (vcpu %d))\n",
                      domid, cpu_id, vcpu_id);
            __insert_into_activation_Q(si, tkt);
        }
        else if (di->tm_muxing_spec.type == GANG_EVENT_TRIG_MUXING) {
            tkt->remaining_time =
                 di->tm_muxing_spec.params.et_muxing_params.active_time;
            //tkt->earliest_start_time =
            //     MAX(di->tm_muxing_spec.params.et_muxing_params.from,
            //         will_resume_at);
                
            tkt->earliest_start_time =
                 __adjust_activation_time(will_resume_at,
                                          di->tm_muxing_spec.params.et_muxing_params.from,
                                          di->tm_muxing_spec.params.et_muxing_params.period);

            tkt->deadline = tkt->earliest_start_time +
                 di->tm_muxing_spec.params.et_muxing_params.period;

            GANG_LOGT("Inserting event-triggered ticket to 'Waiting Set' "
                      "(domain %d on cpu %d (vcpu %d))\n",
                      domid, cpu_id, vcpu_id);

            __insert_into_activation_Q(si, tkt);

            // TODO: Should the event-triggered domain start waiting for an event?
            //__insert_into_waiting_4_event_set(si, tkt);

        }
        else if (di->tm_muxing_spec.type == GANG_BEST_EFFORT_MUXING) {
            // NOTE:
            // The ticket of a best-effort domain in set as follows.
            // 
            // The variables are:
            // be_doms_in_cohort: Number of best-effort domains in the cohort.
            // est: earliest start time
            // n: number of best-effort domains in the local CPU's cohort that
            //    have being processed in this loop. n = [1, be_doms_in_cohort]
            // 
            // Intialization:
            // deadline = est + n * __period_4_be_doms
            // remaining_time = __quantum_4_be_doms
            //
            // Update:
            // deadline += (be_doms_in_cohort * __period_4_be_doms)
            // remaining_time = __quantum_4_be_doms
            //
            // It is important to not that since each CPU iterates over the same
            // array of gang_dom_info_t, each CPU assigns the same deadline to
            // each best-effort domain's ticket. 
            // 


            //tkt->earliest_start_time =
            //    MAX(di->tm_muxing_spec.params.be_muxing_params.from,
            //        will_resume_at);

            tkt->earliest_start_time =
                 __adjust_activation_time(will_resume_at,
                                          di->tm_muxing_spec.params.be_muxing_params.from,
                                          __period_4_be_doms);
            be_doms_counter++; 

            tkt->deadline = tkt->earliest_start_time + 
                            (be_doms_counter * __period_4_be_doms);

            BUG_ON(be_doms_in_cohort[cpu_cohort] <= 0);

            tkt->remaining_time = __quantum_4_be_doms;

            GANG_LOGT("Inserting Best Effort ticket to 'Activation Queue' "
                      "(domain %d on cpu %d (vcpu %d))\n",
                      domid, cpu_id, vcpu_id);
            __insert_into_activation_Q(si, tkt);
        }
        else {
            GANG_LOG("ERROR: Domain %d with invalid time-multiplexing policy.\n", 
                     domid);
            ASSERT(FALSE);
        }
    }

    //GANG_LOGT("Done\n");
}

/**
 * Per-CPU global adjustment info. 
 * It is used to implement a barrier that coordinates the execution of global
 * adjustment across the CPU in the gang-scheduling CPU pool.
 */
typedef struct adj_cpu_info {
    /** Indicates that the CPU has been paused. */
    atomic_t paused;

    /** Indicates that the CPU has completed its adjustment task. */
    atomic_t ack;
} adj_cpu_info_t;

/**
 * Array of per-CPU global adjustment info. 
 */
SUPPRESS_NOT_USED_WARN
static adj_cpu_info_t adj_cpu_info[NR_CPUS];

/**
 * Count of CPUs that has been paused to perform global adjustment. 
 */
SUPPRESS_NOT_USED_WARN
static atomic_t adj_smp_paused_count;

/**
 * Structure that contains the parameters for calling the function
 * __update_local_sched_v0(...).
 */
typedef struct update_local_sched_v0_params {
    gang_dom_info_t** arr;
    uint16_t len;

    cpumask_t* cohorts;
    int* cpu_2_cohort; 
    int* be_doms_in_cohort;
    size_t num_of_cohorts;

    cpumask_t* muxgroups;
    int* cpu_2_muxgroup;
    size_t num_of_muxgroups;

    s_time_t now;
    unsigned int designated_cpu_id;

    int num_of_involved_cpus;

} update_local_sched_v0_params_t;

/**
 * Performs the adjustment to the local scheduler and then pauses the CPU until
 * it's told to continue.
 *
 * XXX TODO: Can't malloc/free in this function (it's an irq)
 */
static void 
__adjust_and_pause_this_cpu(void* params) {

    unsigned long flags;
    unsigned int cpu_id = smp_processor_id();

    update_local_sched_v0_params_t* the_params =
        (update_local_sched_v0_params_t*) params;


    ASSERT(the_params != NULL); 

    local_irq_save(flags);

    GANG_LOGT("Start\n");

    atomic_set(&adj_cpu_info[cpu_id].ack, 1);

    /////////////////////////////////////////////////////////////////////////
    // BARRIER 1: Wait until all the involved CPUs reach this point.
    /////////////////////////////////////////////////////////////////////////
    atomic_inc(&adj_smp_paused_count);

    while (atomic_read(&adj_smp_paused_count) < 
           the_params->num_of_involved_cpus) {
        udelay(BARRIER_SPIN_DELAY_IN_US);
    }

    //GANG_LOGT("AFTER BARRIER 1\n");

    // At this point it is safe to update the global arrays cohorts,
    // cpu_2_cohort, muxgroups, and cpu_2_muxgroup.

    if (cpu_id == the_params->designated_cpu_id) {
        // The designated CPU copies the passed temporary cohort and muxgroup
        // arrays to the global variables. 

        GANG_LOGT("I am the designated CPU\n");

        memcpy(cohorts, the_params->cohorts, sizeof(cpumask_t) * NR_CPUS);
        memcpy(cpu_2_cohort, the_params->cpu_2_cohort, 
               sizeof(int) * NR_CPUS);
        memcpy(be_doms_in_cohort, the_params->be_doms_in_cohort,
               sizeof(int) * NR_CPUS);
        num_of_cohorts = the_params->num_of_cohorts;

        memcpy(muxgroups, the_params->muxgroups, sizeof(cpumask_t) * NR_CPUS);
        memcpy(cpu_2_muxgroup, the_params->cpu_2_muxgroup, 
               sizeof(int) * NR_CPUS);
        num_of_muxgroups = the_params->num_of_muxgroups;
        
        GANG_LOGT("I just updated cohorts and muxgroups.\n");


        for (uint16_t l = 0; l < the_params->len; l++) {
            gang_dom_info_t* di = the_params->arr[l]; 
            __setup_vcpus_for_domain(di);
        }

        GANG_LOGT("I just updated VCPU-to-CPU map of domains.\n");
    }

    /////////////////////////////////////////////////////////////////////////
    // BARRIER 2: Again wait until all the involved CPUs reach this point.
    /////////////////////////////////////////////////////////////////////////
    atomic_inc(&adj_smp_paused_count);
    
    while (atomic_read(&adj_smp_paused_count) < 
           (2 * the_params->num_of_involved_cpus)) {
        udelay(BARRIER_SPIN_DELAY_IN_US);
    }

    //GANG_LOGT("AFTER BARRIER 2\n");

    // At this point we can update the local scheduler because the global arrays
    // have been updated.
    __update_local_sched_v0(the_params->arr, 
                            the_params->len,
                            the_params->now,
                            the_params->designated_cpu_id);
   
    /* Pause until __smp_resume_after_adjust() is called to resume this cpu */
    while (atomic_read(&adj_cpu_info[cpu_id].paused)) {
        udelay(BARRIER_SPIN_DELAY_IN_US);
    }

    // We substract two from adj_smp_paused_count because it was incremented
    // twice, one time on each barrier.
    atomic_sub(2, &adj_smp_paused_count);

    atomic_set(&adj_cpu_info[cpu_id].ack, 0);

    GANG_LOGT("Done\n");

    local_irq_restore(flags); // Restore interrupts.
}

/**
 * Takes control over the CPUs, makes them update their local schedulers and
 * then makes them pause. 
 */
SUPPRESS_NOT_USED_WARN
static void 
__smp_adjust_and_pause(gang_dom_info_t** arr,
                       uint16_t len,
                       cpumask_t* __cohorts,
                       int* __cpu_2_cohort,
                       int* __be_doms_in_cohort,
                       size_t __num_of_cohorts,
                       cpumask_t* __muxgroups,
                       int* __cpu_2_muxgroup,
                       size_t __num_of_muxgroups,
                       s_time_t now,
                       struct cpupool* cpu_pool) {
    int cpu;
    int cpu_count = cpumask_weight(cpu_pool->cpu_valid);

    update_local_sched_v0_params_t params = { 0 };

    // Timeout for all CPUs in the pool to finish the adjustment and enter the
    // pause loop.
    // We give 50% of the 'adj_time_upper_bound' for this.
    int timeout = (__adj_time_upper_bound / ms_2_ns(1) / 2);
    BUG_ON(timeout <= 0);

    //GANG_LOGT("\n");

    // Current CPU cannot belong to the CPU pool for gang-scheduled domains.
    BUG_ON(cpumask_test_cpu(smp_processor_id(), cpu_pool->cpu_valid));

    /* Clear flags that will be used for synchronization */
    for_each_cpu(cpu, cpu_pool->cpu_valid) {
        BUG_ON(!cpu_online(cpu));

        atomic_set(&adj_cpu_info[cpu].ack, 0);
        atomic_set(&adj_cpu_info[cpu].paused, 1);
    }

    atomic_set(&adj_smp_paused_count, 0);

    // Call the per-CPU adjust function on the CPUs of the pool. 
    params.arr = arr;
    params.len = len;

    params.cohorts = __cohorts;
    params.cpu_2_cohort = __cpu_2_cohort;
    params.be_doms_in_cohort = __be_doms_in_cohort;
    params.num_of_cohorts = __num_of_cohorts;

    params.muxgroups = __muxgroups;
    params.cpu_2_muxgroup = __cpu_2_muxgroup;
    params.num_of_muxgroups = __num_of_muxgroups;

    params.now = now;
    params.designated_cpu_id = cpumask_first(cpu_pool->cpu_valid);
    params.num_of_involved_cpus = cpu_count;


    //GANG_LOGT("Before calling __adjust_and_pause_this_cpu(...) on selected CPUs\n");

    // Issue RPC to other CPUs.
    on_selected_cpus(cpu_pool->cpu_valid, 
                     __adjust_and_pause_this_cpu, 
                     &params, 
                     0); // Don't wait!

    //GANG_LOGT("After calling __adjust_and_pause_this_cpu(...) on selected CPUs\n");

    // Wait 'timeout' ms for all CPUs in the pool to finish the adjustment and
    // enter the pause loop.
    // Note: We use (2 * cpu_count) because adj_smp_paused_count in incremented
    // twice in the function __adjust_and_pause_this_cpu(...).
    while ((atomic_read(&adj_smp_paused_count) < (2 * cpu_count)) && 
           (timeout-- > 0)) {
        mdelay(1);
    }

    GANG_LOGT("Selected CPUs finished the adjustment!\n");
    
    /* Record cpus that timed out. Note the first condition here will only
     * trigger if we stopped waiting due to timeout */
    if (atomic_read(&adj_smp_paused_count) < (2 * cpu_count)) {
        GANG_LOGT("Not all CPUs in the pool have completed adjustment and paused.\n");
        console_start_sync();
        printk("\tMissing CPUs: ");

        for_each_cpu(cpu, cpu_pool->cpu_valid) {
            if (cpu != smp_processor_id() && 
                !atomic_read(&adj_cpu_info[cpu].ack)) {
                printk("%d ", cpu);
            }
        }
        printk("\n");
        console_end_sync();
    }
}

/** Makes the CPUs resume. */
SUPPRESS_NOT_USED_WARN
static void 
__smp_resume_after_adjust(struct cpupool* cpu_pool) {

    int cpu;

    // Timeout for all CPUs in the pool to be ready to resume.
    // We give 50% of the 'adj_time_upper_bound' for this.
    int timeout = (__adj_time_upper_bound / ms_2_ns(1) / 2);

    ASSERT(timeout > 0);
    
    cpumask_raise_softirq(cpu_pool->cpu_valid, SCHEDULE_SOFTIRQ);

    for_each_cpu(cpu, cpu_pool->cpu_valid) {
        BUG_ON(!cpu_online(cpu));
        atomic_set(&adj_cpu_info[cpu].paused, 0);
    }

    // Make sure all the CPUs in the pool resume.
    while ((atomic_read(&adj_smp_paused_count) > 0) && (timeout-- > 0)) {
        mdelay(1);
    }

    if (atomic_read(&adj_smp_paused_count) > 0) {
        GANG_LOGT("Not all CPUs in the pool have resumed execution.\n");
        printk("\tMissing CPUs: ");

        for_each_cpu(cpu, cpu_pool->cpu_valid) {
            if (cpu != smp_processor_id() && 
                atomic_read(&adj_cpu_info[cpu].ack)) {
                printk("%d ", cpu);
            }
        }
        printk("\n");
    }
}

/** Sets or fetchs scheduling parameters for all the domains. */
static int 
gang_adjust_global(const struct scheduler* ops, 
                   struct xen_sysctl_scheduler_op* op) {
    // NOTE:
    // At least for the moment we divide the physical CPUs (hardware threads)
    // into two CPU pools:
    // - Pool-0 (the default pool): It hosts the privileged domain (DOM0) with
    //   one (e.g., 0) or at most a few hardware threads.
    // - GangSched-Pool: It hosts gang-scheduled domains on the rest of the
    //   hardware threads. 
    // 
    // Under this configuration, we require that this function should only
    // be called from DOM0. That means that this function should never be called
    // from a CPU that belongs to the GangSched-Pool. 

    // Number of concurrent executions of this function. 
    // It can be either 0 or 1; i.e., there must not be concurrent execution of
    // this function. 
    static atomic_t exec_count = ATOMIC_INIT(0);

    // gang_priv_data_t* pd = GANG_PRIV_DATA(ops);

    struct domain* dom;
    struct cpupool* cpu_pool = NULL;

    xen_sysctl_gang_schedule_t* params;

    int rc = 0;
    int err = 0;
    SUPPRESS_NOT_USED_WARN
    unsigned long flags; 

    int dom_count = 0;

    // Domain info pointer array.
    gang_dom_info_t** di_parr = NULL;

    // Temporary place holder for the new cohorts of hardware threads.
    cpumask_t* __cohorts = NULL;
    int* __cpu_2_cohort = NULL;
    int* __be_doms_in_cohort = NULL;
    size_t __num_of_cohorts; 

    // Temporary place holder for the new multiplexing groups.
    cpumask_t* __muxgroups = NULL;
    int* __cpu_2_muxgroup = NULL;  
    size_t __num_of_muxgroups; 

    // Containers of domains' parameters in internal representation.
    gang_dom_info_t* dom_infos = NULL;
    dom_infos = xzalloc_array(gang_dom_info_t, GANG_SCHED_MAX_DOMAINS);

    GANG_LOGT("Start\n");

    // Verify that this function is being called from DOM0.
    if (current->domain != dom0) {
        printk("WARNING: %s(...) can only be called from DOM0\n", __func__);
        rc = -EPERM;
        goto EXIT_GANG_ADJUST_GLOBAL;
    }

    // Check that this function is being called on a CPU that belongs to the
    // CPUPOOL 0 (the default CPU pool)
    if (current->domain->cpupool != cpupool0) {
        printk("WARNING: %s(...) can only be called from a CPU "
               "that belongs to CPUPOOL0\n", __func__);
        rc = -EPERM;
        goto EXIT_GANG_ADJUST_GLOBAL;
    }

    // Do not allow multiple concurrent executions of this function. 
    // - No more that one processor at a time.
    // - Do not re-entering on this function.
    if (atomic_inc_and_test(&exec_count) > 1) {
        printk("WARNING: Concurrent executions of %s(...) are not allowed!\n",
               __func__);
        atomic_dec(&exec_count);
        rc = -EBUSY;
        goto EXIT_GANG_ADJUST_GLOBAL;
    }   

    // NOTE: 
    // The caller function sched_adjust_global(...) [in file
    // xen/xen/common/schedule.c] 
    // - Obtains the CPU pool using the ID in 'op->cpupool_id' 
    //     cpu_pool = cpupool_get_by_id(op->cpupool_id);
    // - Then, checks/ensures that:
    //     - cpu_pool != NULL
    //     - op->sched_id == cpu_pool->sched->sched_id

    switch (op->cmd) {
    case XEN_SYSCTL_SCHEDOP_putinfo:
    {
        get_xen_guest_handle(params, op->u.sched_gang.params);

        // Basic checks of parameters. 
        if (params->num_dom_entries < 1 ||
            params->num_dom_entries > GANG_SCHED_MAX_DOMAINS) {
            GANG_LOGT("Invalid number of domain entries: %d \n",
                      params->num_dom_entries);
            rc = -EINVAL;
            break;
        }

        // Check no duplicates in the parameters.
        for (uint16_t e = 0; e < (params->num_dom_entries - 1); e++) {
            for (uint16_t j = (e + 1); j < params->num_dom_entries; j++) {
                if (params->dom_entries[e].domid == params->dom_entries[j].domid) { 
                    GANG_LOGT("Domain entries %d-th and %d-th "
                              "with same domain ID %d.\n",
                              e, j, params->dom_entries[e].domid);
                    rc = -EINVAL;
                    goto EXIT_GANG_ADJUST_GLOBAL;
                }
            }
        }

        // Validate each domain's request.
        for (uint16_t e = 0; e < params->num_dom_entries; e++) {

            cpumask_var_t dom_cpumap;
            gang_sched_dom_conf_t* dom_entry = &params->dom_entries[e];
            gang_sched_policy_t* dom_sched_pol = &dom_entry->gang_sched_policy;
           
            int vcpu_count = 0;
            int is_subset;
            int assigned_cpus_2_dom; // Number of CPUs assigned to the domain.

            if (dom_entry->domid <= 0) {
               GANG_LOGT("Domain entry %d-th has an invalid domain ID %d <= 0.\n",
                         e, dom_entry->domid);
               rc = -EINVAL;
               goto EXIT_GANG_ADJUST_GLOBAL;
            }

            dom = get_domain_by_id((domid_t) dom_entry->domid);
            // TODO: Should we use rcu_lock_domain_by_id() instead?
            //       It is more efficient than get_domain_by_id().

            if (dom == NULL) {
                GANG_LOGT("Domain in entry %d (domid: %d) does not exist.\n",
                          e, dom_entry->domid);
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            } 

            if (dom->cpupool->cpupool_id != op->cpupool_id) {
                GANG_LOGT("Domain in entry %d (domid: %d) is not in the " 
                          "right CPU pool.\n", 
                          e, dom_entry->domid);
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

            cpu_pool = dom->cpupool;

            ///////////////////////////////////////////////////////////////////
            // Check that:
            // (VCPUs in domain) == (Domain's max VCPUs) <= (CPUs in the pool)
            ///////////////////////////////////////////////////////////////////

            vcpu_count = __count_vcpus_in_domain(dom);            
            if (vcpu_count > cpumask_weight(cpu_pool->cpu_valid)) {
                GANG_LOGT("Domain in entry %d (domid: %d) has a number of VCPUs (%d) " 
                          "larger than the number of CPUs in the CPU pool (%d).\n",
                          e, dom_entry->domid, 
                          vcpu_count,
                          cpumask_weight(cpu_pool->cpu_valid));
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

            if (dom->max_vcpus > cpumask_weight(cpu_pool->cpu_valid)) {
                GANG_LOGT("Domain in entry %d (domid: %d) has a maximum number of VCPUs (%d) " 
                          "larger than the number of CPUs in the CPU pool (%d).\n",
                          e, dom_entry->domid, 
                          dom->max_vcpus,
                          cpumask_weight(cpu_pool->cpu_valid));
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }
            
            if (vcpu_count != dom->max_vcpus) {
                GANG_LOGT("Domain in entry %d (domid: %d) has a number of VCPUs (%d) "
                          "different from its maximum number of VCPUs (%d).\n",
                          e, dom_entry->domid,
                          vcpu_count,
                          dom->max_vcpus);
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }


            ///////////////////////////////////////////////////////////////////
            // Check that:
            // - the desired CPUs for the domain are in the CPU pool.
            // - the number of VCPUs in the domain is equal to the number of
            //   CPUs assigned to the domain.
            ///////////////////////////////////////////////////////////////////
            err = xenctl_bitmap_to_cpumask(&dom_cpumap, &(dom_entry->cpumap));
            if (err) {
               GANG_LOGT("xenctl_bitmap_to_cpumask(...) failed.\n");
               rc = err;
               goto EXIT_GANG_ADJUST_GLOBAL;
            }

            is_subset = cpumask_subset(dom_cpumap, dom->cpupool->cpu_valid);
            assigned_cpus_2_dom = cpumask_weight(dom_cpumap);

            free_cpumask_var(dom_cpumap);


            if (!is_subset) {
                GANG_LOGT("Desired CPUs for domain in entry %d (domid: %d) "
                          "are not a subset of the CPUs in the CPU pool.\n",
                          e, dom_entry->domid);
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

            if (vcpu_count != assigned_cpus_2_dom) {
                GANG_LOGT("The number of CPUs assigned to domain in entry %d (domid: %d) "
                          "is not the same as its number of VCPUs." 
                          "Assigned CPUs = %d, VCPU count = %d \n",
                          e, dom_entry->domid, assigned_cpus_2_dom, vcpu_count);
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

            /////////////////////////////////////////////////////////////////// 
            // Check time multiplexing policy for the domain. 
            /////////////////////////////////////////////////////////////////// 

            if (!VALIDATE_GANG_SCHED_POLICY_TYPE(dom_sched_pol->type)) {
                GANG_LOGT("The type of the gang scheduling policy for the domain in "
                          "entry %d (domid: %d) is invalid. Type: %s\n",
                          e, dom_entry->domid, 
                          GANG_SCHED_POLICY_2_STR(dom_sched_pol->type));
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

            if (!__validate_gang_sched_policy(dom_sched_pol)) {
                GANG_LOGT("The parameters of the gang scheduling policy for the domain in "
                          "entry %d (domid: %d) are invalid. Type: %s\n",
                          e, dom_entry->domid, 
                          GANG_SCHED_POLICY_2_STR(dom_sched_pol->type));
                rc = -EINVAL;
                goto EXIT_GANG_ADJUST_GLOBAL;
            }

        }

        // Current CPU cannot belong to the CPU pool for gang-scheduled domains.
        BUG_ON(cpumask_test_cpu(smp_processor_id(), cpu_pool->cpu_valid));
 
        // Get current CPU masks and scheduling parameters of domains, and
        // update them.
        dom_count = __get_all_domain_infos(dom_infos, GANG_SCHED_MAX_DOMAINS,
                                           cpu_pool);
        //GANG_LOGT("dom_count = %d\n", dom_count);
        BUG_ON(dom_count == 0);
        BUG_ON(dom_count > GANG_SCHED_MAX_DOMAINS);


        // Update dom_infos structure. Does not actually change anything yet!
        err = __update_domain_infos(dom_infos, dom_count, params); 
        //GANG_LOGT("err = %d\n", err); 
        BUG_ON(err < 0);
 
        // Create array of pointers to domain infos.
        di_parr = xmalloc_array(gang_dom_info_t*, dom_count);  
        if (di_parr == NULL) {
            rc = -ENOMEM;
            goto EXIT_GANG_ADJUST_GLOBAL;
        }

        // Fill in domain pointer array.
        for (int dc = 0; dc < dom_count; dc++) {
            di_parr[dc] = &(dom_infos[dc]);
        }

        // TODO: Implement __validate_resource_allocation(...)
        if (!__validate_resource_allocation(di_parr, (uint16_t) dom_count)) {
            // Domains allocation is invalid!
            rc = -EINVAL;
            goto EXIT_GANG_ADJUST_GLOBAL;
        }

        // Determine cohorts and muxgroups and store them in temporary variables
        // (arrays). They will be written to global variables as part of the
        // adjustment initiated in __smp_adjust_and_pause(...).
        // See the globals cohorts, cpu_2_cohort and muxgroups defined in this
        // file.
        
        __cohorts = xzalloc_array(cpumask_t, NR_CPUS);
        __cpu_2_cohort = xmalloc_array(int, NR_CPUS);
        __be_doms_in_cohort = xmalloc_array(int, NR_CPUS);

        __muxgroups = xzalloc_array(cpumask_t, NR_CPUS);
        __cpu_2_muxgroup = xmalloc_array(int, NR_CPUS);

        __populate_cohorts_and_muxgroups(di_parr,
                                         (uint16_t) dom_count,
                                         __cohorts,
                                         __cpu_2_cohort,
                                         __be_doms_in_cohort,
                                         &__num_of_cohorts,
                                         __muxgroups,
                                         __cpu_2_muxgroup,
                                         &__num_of_muxgroups);


        // NOTE: 
        // We assume that a new domain, coming from another CPU pool, has been
        // paused before calling this function.  


        //GANG_LOGT("About to adjust and pause\n");

        ////////////////////////////////////////////////////////////////////////
        // Pause all CPUs in the gang-scheduling pool in order to make the
        // global adjustment. The global adjustment involves 3 major steps:
        // - Update the global arrays of cohorts and muxgroups.
        // - Update the CPU affinity of the VCPUs of each domain.
        // - Update each per-CPU local scheduler.
        ////////////////////////////////////////////////////////////////////////
        __smp_adjust_and_pause(di_parr, 
                               (uint16_t) dom_count,
                               __cohorts,
                               __cpu_2_cohort,
                               __be_doms_in_cohort,
                               __num_of_cohorts,
                               __muxgroups,
                               __cpu_2_muxgroup,
                               __num_of_muxgroups,
                               NOW(),
                               cpu_pool);

        local_irq_save(flags);
        watchdog_disable();


        ////////////////////////////////////////////////////////////////////////
        // Unpause the domains that were involved in the adjustment as well as
        // their VCPUs.
        ////////////////////////////////////////////////////////////////////////
        for (int dc = 0; dc < dom_count; dc++) {
    
            dom = di_parr[dc]->domain;            

            // Each domain coming from another CPU pool is unpaused.
            atomic_set(&dom->pause_count, 0);

            if (dom->controller_pause_count) {
                dom->controller_pause_count = 0;
            }

            // Mark each VCPU of the domain as unblocked, and make it runnable.
            {
                struct vcpu* vcpu;
                for_each_vcpu(dom, vcpu) {
                    vcpu->poll_evtchn = 0;
                    clear_bit(_VPF_blocked, &vcpu->pause_flags);

                    if (vcpu->runstate.state >= RUNSTATE_blocked ) {
                        s_time_t new_entry_time = NOW();
                        s_time_t delta = 
                            new_entry_time - vcpu->runstate.state_entry_time;
                        if (delta > 0) {
                            vcpu->runstate.time[vcpu->runstate.state] += delta;
                            vcpu->runstate.state_entry_time = new_entry_time;
                        }

                        vcpu->runstate.state = RUNSTATE_runnable;
                    }
                }
            }

        }

        //GANG_LOGT("About to restart\n");

        // Restart CPUs (they were paused in __smp_adjust_and_pause)
        __smp_resume_after_adjust(cpu_pool);

        watchdog_enable();
        local_irq_restore(flags);

        ////////////////////////////////////////////////////////////////////////
        // At this point all CPUs in the gang-scheduling pool should have
        // resumed.
        ////////////////////////////////////////////////////////////////////////

        break;
    }
    case XEN_SYSCTL_SCHEDOP_getinfo:
    {
        get_xen_guest_handle(params, op->u.sched_gang.params);
        params->num_dom_entries = 0;

        cpu_pool = cpupool_get_by_id(op->cpupool_id);

        // Current CPU cannot belong to the CPU pool for gang-scheduled domains.
        BUG_ON(cpumask_test_cpu(smp_processor_id(), cpu_pool->cpu_valid));

        dom_count = __get_all_domain_infos(dom_infos, GANG_SCHED_MAX_DOMAINS,
                                           cpu_pool);
        GANG_LOGT("dom_count = %d\n", dom_count);
        BUG_ON(dom_count > GANG_SCHED_MAX_DOMAINS);

        // Copy domain_infos onto xen_sysctl_gang_schedule_t* params.
        for (int c = 0; c < dom_count; c++) { 

            gang_dom_info_t* di = &dom_infos[c];
            params->dom_entries[c].domid = __get_domid_from_dom_info(di);

            err = cpumask_to_xenctl_bitmap(&params->dom_entries[c].cpumap,
                                           &di->cpumask);
            if (err) {
                params->dom_entries[c].domid = -(params->dom_entries[c].domid);
                rc = err;
            }

            memcpy(&params->dom_entries[c].gang_sched_policy,
                   &di->tm_muxing_spec,
                   sizeof(gang_sched_policy_t));
        }

        params->num_dom_entries = (uint16_t) dom_count;

        break;
    }
    }
 
EXIT_GANG_ADJUST_GLOBAL:

    xfree(__cohorts);
    xfree(__cpu_2_cohort);
    xfree(__be_doms_in_cohort);

    xfree(__muxgroups);
    xfree(__cpu_2_muxgroup);

    xfree(di_parr);
    xfree(dom_infos);

    atomic_dec(&exec_count);

    GANG_LOGT("Done!\n");

    return rc;
}

///////////////////////////////////////////////////////////////////////////////

/** 
 * Only fetches domain scheduling parameters. 
 * It must not set any parameter. 
 */
static int 
gang_adjust(const struct scheduler* ops, struct domain* p, 
            struct xen_domctl_scheduler_op* op) {
    // Actually there is not real need to implement this function because
    // gang_adjust_global(...) provides similar functionality via
    // XEN_SYSCTL_SCHEDOP_getinfo command. 

    return -ENOSYS;
}

/** Dumps all domains on the specified CPU. */
static void 
gang_dump_cpu_state(const struct scheduler* ops, int i) {
    // TODO: Implement
}

static void 
gang_dump_settings(const struct scheduler* ops) {
    // TODO: Implement
}

static struct gang_priv_data __gang_priv_data;

const struct scheduler sched_gang_def = {
    .name           = "Integrated Gang Scheduler",
    .opt_name       = "gang",
    .sched_id       = XEN_SCHEDULER_GANG,
    .sched_data     = &__gang_priv_data,
  
    .global_init    = gang_global_init,

    .init           = gang_init,
    .deinit         = gang_deinit,
 
    .init_domain    = gang_init_domain,
    .destroy_domain = gang_destroy_domain,

    .alloc_domdata  = gang_alloc_domdata,
    .free_domdata   = gang_free_domdata,

    // This is to avoid calls to gang_insert_vcpu() from CPUs other than the
    // local CPU. 
    .insert_vcpu    = NULL, // gang_insert_vcpu,

    .remove_vcpu    = gang_remove_vcpu,

    .alloc_vdata    = gang_alloc_vdata,
    .free_vdata     = gang_free_vdata,

    .alloc_pdata    = gang_alloc_pdata,
    .free_pdata     = gang_free_pdata,

    .do_schedule    = gang_do_schedule,
    .yield          = gang_yield,

    .wake           = NULL, //gang_wake,
    .sleep          = NULL, //gang_sleep,

    .adjust         = gang_adjust,  
    .adjust_global  = gang_adjust_global,

    .dump_settings  = gang_dump_settings,
    .dump_cpu_state = gang_dump_cpu_state,

};

REGISTER_SCHEDULER(sched_gang_def);
