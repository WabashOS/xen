/*
 * sched_test.c
 * 
 * Unit tests for schedulability analysis. 
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


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include <xenctrl.h>
#include <xentoollog.h>

#include "../gs_sched_test.h"


void test0(void);
void test1(void);
void test2(void);
void test3(void);


/** Main function. */
int main(int argc, char** argv) {
 
    printf("Running schedulability tests (program %s) ... \n", argv[0]);

    test0();
    test1();
    test2();
    test3();

    return 0;
}


void test0() {

    // All time units are in milliseconds.

    int rc = 0;

    size_t be_reserve = 10;
    uint64_t be_basic_period = 100; // ms
    size_t cpu_count = 8;

    int32_t cpus[] = {0, 1, 2, 3, 4, 5, 6, 7};

    gs_dominfo_t* arr[1] = { 0 };

    gs_dominfo_t di0 = { 0 };


    di0.domid = 0;
    di0.cpus = cpus;
    di0.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di0.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di0.gang_sched_policy.params.tt_muxing_params.period = 100; 
    di0.gang_sched_policy.params.tt_muxing_params.active_time = 50;

    arr[0] = &di0;

    printf("[%s] running ...\n", __func__);
    rc = are_schedulable(arr, 
                         1, 
                         be_reserve, 
                         be_basic_period, 
                         cpu_count);

    if (rc == 0) {
        printf("[%s] passed.\n", __func__);
    }
    else {
        printf("[%s] failed.\n", __func__);
    }
    
}


void test1() {

    // All time units are in milliseconds.

    int rc = 0;

    size_t be_reserve = 10;
    uint64_t be_basic_period = 100; // ms
    size_t cpu_count = 8;

    int32_t cpus[] = {0, 1, 2, 3, 4, 5, 6, 7};

    gs_dominfo_t* arr[2] = { 0 };

    gs_dominfo_t di0 = { 0 };
    gs_dominfo_t di1 = { 0 };


    di0.domid = 0;
    di0.cpus = cpus;
    di0.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di0.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di0.gang_sched_policy.params.tt_muxing_params.period = 100; 
    di0.gang_sched_policy.params.tt_muxing_params.active_time = 95;
    arr[0] = &di0;

    di1.domid = 1;
    di1.cpus = cpus;
    di1.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di1.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di1.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[1] = &di1;

    printf("[%s] running ...\n", __func__);
    rc = are_schedulable(arr, 
                         2, 
                         be_reserve, 
                         be_basic_period, 
                         cpu_count);

    if (rc != 0) {
        // The schedulability test is supposed to fail!
        printf("[%s] passed.\n", __func__);
    }
    else {
        printf("[%s] failed.\n", __func__);
    }
    
}

/** 
 * Tests the schedulability of a set of domains that was reported in a paper
 * submission.
 */
void test2() {

    // All time units are in milliseconds.

    int rc = 0;

    size_t be_reserve = 10;
    uint64_t be_basic_period = 100; // ms
    size_t cpu_count = 4;

    int32_t cpus[] = {0, 1, 2, 3};

    gs_dominfo_t* arr[7] = { 0 };

    gs_dominfo_t di0 = { 0 };
    gs_dominfo_t di1 = { 0 };
    gs_dominfo_t di2 = { 0 };
    gs_dominfo_t di3 = { 0 };
    gs_dominfo_t di4 = { 0 };
    gs_dominfo_t di5 = { 0 };
    gs_dominfo_t di6 = { 0 };

    // TT: 15/150
    di0.domid = 0;
    di0.cpus = cpus;
    di0.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di0.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di0.gang_sched_policy.params.tt_muxing_params.period = 150; 
    di0.gang_sched_policy.params.tt_muxing_params.active_time = 15;
    arr[0] = &di0;

    // TT: 40/200
    di1.domid = 1;
    di1.cpus = cpus;
    di1.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di1.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di1.gang_sched_policy.params.tt_muxing_params.period = 200; 
    di1.gang_sched_policy.params.tt_muxing_params.active_time = 40;
    arr[1] = &di1;

    // ET: 40/200
    di2.domid = 2;
    di2.cpus = cpus;
    di2.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di2.gang_sched_policy.type = GANG_EVENT_TRIG_MUXING;
    di2.gang_sched_policy.params.et_muxing_params.period = 100; 
    di2.gang_sched_policy.params.et_muxing_params.active_time = 10;
    arr[2] = &di2;

    // ET: 60/200
    di3.domid = 3;
    di3.cpus = cpus;
    di3.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di3.gang_sched_policy.type = GANG_EVENT_TRIG_MUXING;
    di3.gang_sched_policy.params.et_muxing_params.period = 200; 
    di3.gang_sched_policy.params.et_muxing_params.active_time = 60;
    arr[3] = &di3;


    // 3 BE domains

    di4.domid = 4;
    di4.cpus = cpus;
    di4.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di4.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di4.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[4] = &di4;


    di5.domid = 5;
    di5.cpus = cpus;
    di5.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di5.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di5.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[5] = &di5;


    di6.domid = 6;
    di6.cpus = cpus;
    di6.num_of_cpus = sizeof(cpus)/sizeof(cpus[0]);
    di6.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di6.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[6] = &di6;


    printf("[%s] running ...\n", __func__);
    rc = are_schedulable(arr, 
                         7, 
                         be_reserve, 
                         be_basic_period, 
                         cpu_count);

    if (rc == 0) {
        // The schedulability test is supposed to pass!
        printf("[%s] passed.\n", __func__);
    }
    else {
        printf("[%s] failed.\n", __func__);
    }
    
}

/** 
 * Tests the schedulability of a set of domains that was reported in a paper
 * submission.
 */
void test3() {

    // All time units are in milliseconds.

    int rc = 0;

    size_t be_reserve = 10;
    uint64_t be_basic_period = 100; // ms
    size_t cpu_count = 40;

//    int32_t cpus[] = {
//         0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
//        10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
//        20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
//        30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
//    };

    gs_dominfo_t* arr[7] = { 0 };
    gs_dominfo_t di0 = { 0 };
    gs_dominfo_t di1 = { 0 };
    gs_dominfo_t di2 = { 0 };
    gs_dominfo_t di3 = { 0 };
    gs_dominfo_t di4 = { 0 };
    gs_dominfo_t di5 = { 0 };
    gs_dominfo_t di6 = { 0 };


    int32_t di0_cpus[] = {  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, };
    int32_t di1_cpus[] = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, };
    int32_t di2_cpus[] = { 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, };
    int32_t di3_cpus[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, };
    int32_t di4_cpus[] = { 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, };
    int32_t di5_cpus[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, };
    int32_t di6_cpus[] = { 35, 36, 37, 38, 39, };

    // 1 NoMux domain

    di0.domid = 0;
    di0.cpus = di0_cpus;
    di0.num_of_cpus = sizeof(di0_cpus)/sizeof(di0_cpus[0]);
    di0.gang_sched_policy.type = GANG_NO_MUXING;
    arr[0] = &di0;

    // 2 TT domains

    di1.domid = 1;
    di1.cpus = di1_cpus;
    di1.num_of_cpus = sizeof(di1_cpus)/sizeof(di1_cpus[0]);
    di1.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di1.gang_sched_policy.params.tt_muxing_params.period = 150; 
    di1.gang_sched_policy.params.tt_muxing_params.active_time = 15;
    arr[1] = &di1;

    di2.domid = 2;
    di2.cpus = di2_cpus;
    di2.num_of_cpus = sizeof(di2_cpus)/sizeof(di2_cpus[0]);
    di2.gang_sched_policy.type = GANG_TIME_TRIG_MUXING;
    di2.gang_sched_policy.params.tt_muxing_params.period = 200; 
    di2.gang_sched_policy.params.tt_muxing_params.active_time = 60;
    arr[2] = &di2;

    // 2 ET domains

    di3.domid = 3;
    di3.cpus = di3_cpus;
    di3.num_of_cpus = sizeof(di3_cpus)/sizeof(di3_cpus[0]);
    di3.gang_sched_policy.type = GANG_EVENT_TRIG_MUXING;
    di3.gang_sched_policy.params.et_muxing_params.period = 200; 
    di3.gang_sched_policy.params.et_muxing_params.active_time = 40;
    arr[3] = &di3;

    di4.domid = 4;
    di4.cpus = di4_cpus;
    di4.num_of_cpus = sizeof(di4_cpus)/sizeof(di4_cpus[0]);
    di4.gang_sched_policy.type = GANG_EVENT_TRIG_MUXING;
    di4.gang_sched_policy.params.et_muxing_params.period = 200; 
    di4.gang_sched_policy.params.et_muxing_params.active_time = 40;
    arr[4] = &di4;

    // 2 BE domains

    di5.domid = 5;
    di5.cpus = di5_cpus;
    di5.num_of_cpus = sizeof(di5_cpus)/sizeof(di5_cpus[0]);
    di5.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di5.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[5] = &di5;

    di6.domid = 6;
    di6.cpus = di6_cpus;
    di6.num_of_cpus = sizeof(di6_cpus)/sizeof(di6_cpus[0]);
    di6.gang_sched_policy.type = GANG_BEST_EFFORT_MUXING;
    // Weight given just to pass parameter validation.
    di6.gang_sched_policy.params.be_muxing_params.weight = 128;
    arr[6] = &di6;


    printf("[%s] running ...\n", __func__);
    rc = are_schedulable(arr, 
                         7, 
                         be_reserve, 
                         be_basic_period, 
                         cpu_count);

    if (rc == 0) {
        // The schedulability test is supposed to pass!
        printf("[%s] passed.\n", __func__);
    }
    else {
        printf("[%s] failed.\n", __func__);
    }
    
}
