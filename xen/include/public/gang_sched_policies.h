/*
 * sched_gang_policies.h
 *
 * Declarations of gang-scheduling (time-multiplexing) policies.
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
 * Copyright (c) 2009 Juan A. Colmenares <juancol@eecs.berkeley.edu>
 * Copyright (c) 2009 The Regents of the University of California.
 */

#ifndef __GANG_SCHED_POLICIES_H__
#define __GANG_SCHED_POLICIES_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enumeration of the time multiplexing policies for gang scheduling.
 *
 * A policy has precedence over other policies. The relative precedence of a
 * time-multiplexing policy is indicated by the assigned number in this
 * enumeration: the lower the number, the higher the precedence.  For example, a
 * message-triggered domains should not disturb the activation and execution of
 * any time-triggered domain already admitted in the system.
 *
 * We assume that the gang scheduler and other parts of the system, particularly
 * Admission Control and Dynamic Resource Management, work together in order to
 * enforce this precedence rule. 
 *
 * Note that the gang scheduling implementation is *not* based on fixed
 * priorities.
 */
typedef enum gang_sched_policy_type {
    GANG_SCHED_POLICY_NOT_SET = 0,
    GANG_NO_MUXING            = 1,
    GANG_TIME_TRIG_MUXING     = 2,
    GANG_EVENT_TRIG_MUXING    = 3,
    GANG_BEST_EFFORT_MUXING   = 4,
    /**
     * Number of gang scheduling policies.
     * Always at the end of the enumeration.
     */
    NUM_OF_GANG_SCHED_POLICIES,
} gang_sched_policy_type_t;



/*!
 * \var GANG_NO_MUXING
 *
 * Indicates that the time multiplexing policy is null. In other words, the
 * domain will be assigned the specified hardware threads and other resources
 * permanently and will not be subject to time multiplexing.
 */

/*!
 * \var GANG_TIME_TRIG_MUXING
 *
 * Indicates that the domain will be activated in a time-triggered manner.
 *
 * A domain with this time multiplexing policy cannot disturb the execution of
 * any non-multiplexed domains admitted in the system (by definition).
 */

/*!
 * \var GANG_EVENT_TRIG_MUXING
 *
 * Indicates that the domain will be activated by the arrivals of designated
 * events.
 *
 * A domain with this time multiplexing policy will be activated ONLY IF its
 * activation will not affect the execution of any non-multiplexed domains (by
 * definition) and time-triggered domains admitted in the system.
 */

/*!
 * \var GANG_BEST_EFFORT_MUXING
 *
 * Indicates that the domain will be scheduled in a fairly manner with other domains
 * using the same fair scheduling policy.
 *
 * A domain with this time multiplexing policy cannot disturb the activation and
 * execution of other domains with higher precedence that have been admitted in
 * the system (i.e., non-multiplex domains, time-triggered domains, and
 * message-triggered domains).
 */


/** 
 * Macro that tells whether or not the gang scheduling policy type is valid.
 * The type of __pol_type must be gang_sched_policy_type_t.
 */
#define VALIDATE_GANG_SCHED_POLICY_TYPE(__pol_type) \
    (GANG_SCHED_POLICY_NOT_SET < (__pol_type) &&    \
     (__pol_type) < NUM_OF_GANG_SCHED_POLICIES)


/** 
 * Macro that returns a string representation of a gang scheduling policy. 
 * The type of __pol_type must be gang_sched_policy_type_t.
 */
#define GANG_SCHED_POLICY_2_STR(__pol_type) \
  ((__pol_type) == GANG_SCHED_POLICY_NOT_SET ? "GANG_SCHED_POLICY_NOT_SET" : \
   (__pol_type) == GANG_NO_MUXING ? "GANG_NO_MUXING"                       : \
   (__pol_type) == GANG_TIME_TRIG_MUXING ? "GANG_TIME_TRIG_MUXING"         : \
   (__pol_type) == GANG_EVENT_TRIG_MUXING ? "GANG_EVENT_TRIG_MUXING"       : \
   (__pol_type) == GANG_BEST_EFFORT_MUXING ? "GANG_BEST_EFFORT_MUXING"     : \
   "INVALID")


/** Parameters for the no-multiplexing policy. */
typedef struct no_muxing_parameters {
    /** Start time (in ns) for the domain. */
    uint64_t from;
} no_muxing_params_t;


/** Parameters for the time-triggering policy. */
typedef struct time_triggering_parameters {
    /**
     * Start time (in ns) for the series activations of the domain.
     * Activation are only possible after this time.
     */
    uint64_t from;

    /** Activation period period (in ns). */
    uint64_t period;

    /**
     * Amount of time (in ns) the domain will be in the active state within a period.
     * Must be <= 'every'.
     */
    uint64_t active_time;

    /** 
     * Indicates whether or not the domain is eligible to fill the "holes" left
     * by other gang-scheduled domains.
     */
    char space_filling;
    
#if 0
    /**
     * If 'false', the domain will be activated only once per period and will be
     * active for a time slice of length equal to 'active_time'.  If 'true', the
     * 'active_time' may be accumulated in multiple activations in the same
     * period.
     *
     * Note that time-triggered domains that do not allow time fragments have
     * precedence over those that allow time fragments.
     */
    /* TODO: Implement this feature. */
    char allow_fragments; 
#endif

} tt_muxing_params_t;


/**
 * Parameters for the event-triggering policy.
 */
typedef struct event_triggering_parameters {
    /**
     * Start time (in ns) for the series activations of the domain.
     * Activation are only possible after this time.
     */
    uint64_t from;
 
    /** Minimum activation period period (in ns). */
    uint64_t period;

    /**
     * Maximum amount of processor time (in ns) the domain is allowed within a
     * period. Must be < 'period'.
     * The quotient 'active_time'/'period' represents the CPU bandwidth
     * allocated to the domain.
     */
    uint64_t active_time;

    /** 
     * Indicates whether or not the domain is eligible to fill the "holes" left
     * by other gang-scheduled domains.
     */
    char space_filling;

} et_muxing_params_t;



/** 
 * Parameters for the best-effort policy.
 */
typedef struct best_effort_muxing_parameters {

    /** Start time (in ns) for the domain. */
    uint64_t from;

    /** */
    uint16_t weight;

    /** 
     * Indicates whether or not the domain is eligible to fill the "holes" left
     * by other gang-scheduled domains.
     */
    char space_filling;

} be_muxing_params_t;


/** Specification of the gang scheduling policy for a domain. */
typedef struct gang_sched_policy {
    /** The type of the time-multiplexing policy. */
    gang_sched_policy_type_t type; 
    union {
        no_muxing_params_t no_muxing_params;
        tt_muxing_params_t tt_muxing_params;
        et_muxing_params_t et_muxing_params;
        be_muxing_params_t be_muxing_params; 
    } params;
} gang_sched_policy_t;

#ifdef __cplusplus
}
#endif

#endif /* __GANG_SCHED_POLICIES_H__ */


