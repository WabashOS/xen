/*
 * gsc.c
 * 
 * Simple program to control the gang scheduler and gang-scheduled domains.
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

#include "gs_ctrl.h"
#include "gs_utils.h"



#define GSC_VERBOSE (1)


/** Xen control interface. */
static xc_interface* xch = NULL;

/** Logger. */
static xentoollog_logger_stdiostream* logger;

/** Log message level. */
static xentoollog_level minmsglevel = XTL_PROGRESS;


/** Prints usage information on screen. */
static
void print_usage(void) {
    printf("USAGE:\n"); 
    printf("  gsc -p cpupoolid -d domid -c <CPU list> -t <time muxing policy and its parameters>\n");
    printf("      Sets the configuration parameters of a gang-scheduled domain.\n");
    printf("      CPU list: CPU IDs separated with commas (e.g., 1,4,2,5)\n" 
           "      time muxing policy and its parameters separated with commas: \n" 
           "          No muxing: no\n"
           "          Time triggered: tt, period, active time, [sf] \n"
           "          Event triggered: et, period, active time, [sf] \n"
           "          Best effort: be, weight, [sf] \n"
           "      sf: space filling\n"
           "      period and active time: time values in milliseconds. active time <= period\n"
           "      Legal weights range from 1 to 65535 and the default is 256.\n\n");

    printf("  gsc -p cpupoolid -d domid\n");
    printf("      Prints the configuration parameters of a gang-scheduled domain.\n\n");

    printf("  gsc -p cpupoolid\n");
    printf("      Prints the configuration parameters of all gang-scheduled domains.\n\n");

}



/** Parses and validates an integer parameter. */
static
int get_int_param(char* optname, char* valstr) {
    int val;    
    char* endptr;

    if (valstr == NULL) {
        fprintf(stderr, "No digits were found for option %s\n", optname);
        exit(EXIT_FAILURE);
    }

    errno = 0; // To distinguish success/failure after call.

    val = strtol(valstr, &endptr, 10);

    // Check for various possible errors.
    if ((errno == ERANGE && 
        (val == LONG_MAX || val == LONG_MIN)) || 
        (errno != 0 && val == 0)) {
        char* strerr = strerror(errno);
        fprintf(stderr, "ERROR: Argument of option %s is invalid: %s\n", 
                optname, strerr);
        exit(EXIT_FAILURE);
    }

    if (*endptr != '\0') {
        fprintf(stderr, "ERROR: Argument of option %s is invalid: %s\n", 
                optname, valstr);
        exit(EXIT_FAILURE);
    }

   return val; 
}


/** Parses and validates a comma separated CPU IDs. */
static
int* get_cpus(char* str, int* num_of_cpus) {
    char seps[] = ",";
    char* token;
    int* cpus = NULL;
    int nr_cpus, i;

    char* str2 = strdup(str);

    nr_cpus = 0;
    token = strtok(str, seps);
    while (token != NULL) {
        //printf("token %d: %s\n", nr_cpus, token);
        nr_cpus++; 
        token = strtok(NULL, seps);
    }

    cpus = calloc(sizeof(int), nr_cpus);
    assert(cpus != NULL);

    i = 0;
    token = strtok(str2, seps);
    while (token != NULL) {
        //printf("token %d: %s\n", i, token);
        cpus[i++] = get_int_param("-c", token);
        token = strtok(NULL, seps);
    }

    *num_of_cpus = nr_cpus;
    return cpus;
}


/**
 * Parses, validates, and populates the gang scheduling policy.
 */
static
void get_gang_sched_policy(char* str, gang_sched_policy_t* tm_pol) {
    char seps[] = ",";
    char* token;
    int i;

    char* pol_selector_str = NULL;
    char* param1 = NULL;
    char* param2 = NULL; 
    char* param3 = NULL; 

    gang_sched_policy_type_t pol_type;
    int period_in_ms = 0;
    int active_time_in_ms = 0;
    int weight = 0;
    char space_filling = 0;
 
    assert(tm_pol != NULL);

    i = 0;
    token = strtok(str, seps);
    while (token != NULL) {
        //printf("token %d: %s\n", i, token);

        if (i == 0) {
           pol_selector_str = strdup(token); 
        }
        else if (i == 1) {
            param1 = strdup(token);
        }
        else if (i == 2) {
            param2 = strdup(token);
        }
        else if (i == 3) {
            param3 = strdup(token);
        }

        i++;

        token = strtok(NULL, seps);
    }

    if (i > 4) {
        fprintf(stderr, "ERROR: Extra parameters for the time-multiplexing policy.\n");
        exit(EXIT_FAILURE);
    }

    //printf("pol_selector_str = %s\n", pol_selector_str);
    //printf("param1 = %s\n", param1);
    //printf("param2 = %s\n", param2);
    //printf("param3 = %s\n", param3);
    
    if (!strcmp("no", pol_selector_str)) {
        pol_type = GANG_NO_MUXING;
        if (i != 1) {
            fprintf(stderr, "ERROR: No-multiplexing policy receives 1 parameters.\n");
            exit(EXIT_FAILURE);
        }
    }
    else if (!strcmp("tt", pol_selector_str)) {
        pol_type = GANG_TIME_TRIG_MUXING;
        if (i != 3 && i != 4) {
            fprintf(stderr, "ERROR: Time-triggering policy receives 3 or 4 parameters.\n");
            exit(EXIT_FAILURE);
        }
    }
    else if (!strcmp("et", pol_selector_str)) {
        pol_type = GANG_EVENT_TRIG_MUXING;
        if (i != 3 && i != 4) {
            fprintf(stderr, "ERROR: Event-triggering policy receives 3 or 4 parameters.\n");
            exit(EXIT_FAILURE);
        }
    }
    else if (!strcmp("be", pol_selector_str)) {
        pol_type = GANG_BEST_EFFORT_MUXING;
        if (i != 2 && i != 3) {
            fprintf(stderr, "ERROR: Best-effort policy receives 2 or 3 parameters.\n");
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr, "ERROR: Invalid time-multiplexing policy.\n");
        exit(EXIT_FAILURE);
    }


    if (pol_type == GANG_TIME_TRIG_MUXING || pol_type == GANG_EVENT_TRIG_MUXING) {
        period_in_ms = get_int_param("period", param1);
        active_time_in_ms = get_int_param("active_time", param2);

        if (period_in_ms <= 0) {
            fprintf(stderr, "ERROR: Period value cannot be <= 0.\n");
            exit(EXIT_FAILURE);
        }
     
       if (active_time_in_ms <= 0) {
            fprintf(stderr, "ERROR: Active time value cannot be <= 0.\n");
            exit(EXIT_FAILURE);
        }

        if (active_time_in_ms >= period_in_ms) {
            fprintf(stderr, "ERROR: Active time must be lesser than period.\n");
            exit(EXIT_FAILURE);
        }


        if (param3 != NULL) {
            if (!strcmp("sf", param3)) {
                space_filling = 1;
            }
            else {
                fprintf(stderr, "ERROR: Invalid parameter for space filling flag.\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    else if (pol_type == GANG_BEST_EFFORT_MUXING) {
        weight = get_int_param("weight", param1);
        if (weight <= 0 || weight > USHRT_MAX )  {
            fprintf(stderr, "ERROR: Invalid value for the weight parameter.\n");
            exit(EXIT_FAILURE);
        } 

        if (param2 != NULL) {
            if (!strcmp("sf", param2)) {
                space_filling = 1;
            }
            else {
                fprintf(stderr, "ERROR: Invalid parameter for space filling flag.\n");
                exit(EXIT_FAILURE);
            }
        }
    }
  
    bzero(tm_pol, sizeof(gang_sched_policy_t));

    tm_pol->type = pol_type;
    if (pol_type == GANG_NO_MUXING) {
       tm_pol->params.no_muxing_params.from = 0; 
    }
    else if (pol_type == GANG_TIME_TRIG_MUXING) {
        tm_pol->params.tt_muxing_params.from = 0; 
        tm_pol->params.tt_muxing_params.period = 
            ((uint64_t) period_in_ms) * 1000000UL;
        tm_pol->params.tt_muxing_params.active_time = 
            ((uint64_t) active_time_in_ms) * 1000000UL;
        tm_pol->params.tt_muxing_params.space_filling = space_filling; 
    }
    else if (pol_type == GANG_EVENT_TRIG_MUXING) {
        tm_pol->params.et_muxing_params.from = 0; 
        tm_pol->params.et_muxing_params.period = 
            ((uint64_t) period_in_ms) * 1000000UL;
        tm_pol->params.et_muxing_params.active_time = 
            ((uint64_t) active_time_in_ms) * 1000000UL;
        tm_pol->params.et_muxing_params.space_filling = space_filling; 
    }
    else if (pol_type == GANG_BEST_EFFORT_MUXING) {
        tm_pol->params.be_muxing_params.from = 0; 
        tm_pol->params.be_muxing_params.weight = (uint16_t) weight; 
        tm_pol->params.be_muxing_params.space_filling = space_filling; 
    } 

    return;
}



/** Command codes. */
enum commands {
    /** List of input parameters is invalid. */
    CMD_INVALID = -1,  

    /** Set the configuration parameters of a given domain. */
    CMD_SET_PARAMS_OF_DOMAIN,

    /**  
     * Get the configuration parameters of a given domain and print it on
     * console. 
     */
    CMD_GET_PARAMS_OF_DOMAIN,

    /**  
     * Get the configuration parameters of all the domains and print them on
     * console. 
     */
    CMD_GET_PARAMS_OF_ALL_DOMAINS
};


/**
 * Processes the command-line input parameters.
 *
 * @param[in] argc number of arguments. 
 * @param[in] argv array of argument NULL-terminated strings.
 * @param[in,out] Identifier of the CPU pool. Caller must allocate.
 * @param[in,out] Identifier of the domain. Caller must allocate
 * @param[in,out] Domain's configuration parameters. Caller must allocate.
 * @return the command to be executed.
 * @note{This function makes the program exit if an error occurs.}
 */
static
int process_args(int argc, 
                 char** argv, 
                 int* cpupoolid, 
                 int* domid, 
                 gang_sched_params_t* gs_params) {

    int cmd = CMD_INVALID;
    int c;

    // Input strings
    char* cpupoolid_str = NULL;
    char* domid_str = NULL;
    char* cpu_list = NULL;
    char* gang_sched_pol_str = NULL; 
 
    // Input values
    int* cpus = NULL;
    int num_of_cpus = 0;
    gang_sched_policy_t gang_sched_pol;

    *cpupoolid = -1;
    *domid = -1;

    if (argc == 1) {
      print_usage();
      exit(EXIT_FAILURE);
    } 

    while ((c = getopt(argc, argv, "p:d:c:t:")) != -1) {
        switch (c) {
            case 'p':
                if (cpupoolid_str == NULL) {
                    cpupoolid_str = optarg;
                } 
                break;
            case 'd':
                if (domid_str == NULL) {
                    domid_str = optarg;
                }
                break;
            case 'c':
                if (cpu_list == NULL) {
                    cpu_list = optarg;
                }
                break;
            case 't':
                if (gang_sched_pol_str == NULL) {
                    gang_sched_pol_str = optarg;
                }
                break;
            case '?':
                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                }
                else {
                    fprintf(stderr,
                            "Unknown option character `\\x%x'.\n",
                            optopt);
                }
                print_usage();
                exit(EXIT_FAILURE);
                break;
            default:
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                print_usage();
                exit(EXIT_FAILURE);
        }
    }

    *cpupoolid = get_int_param("-p", cpupoolid_str);
    if (*cpupoolid < 0) {
        fprintf(stderr, "ERROR: CPU pool ID < 0.\n");
        exit(EXIT_FAILURE);
    }

    if (GSC_VERBOSE) {
        printf("cpupoolid = %d\n", *cpupoolid);
    }


    if (domid_str != NULL) {
        *domid = get_int_param("-d", domid_str);
        if (*domid <= 0) {
            fprintf(stderr, "ERROR: Domain ID <= 0.\n");
            exit(EXIT_FAILURE);
        }

        if (GSC_VERBOSE) {
            printf("domid = %d\n", *domid);
        }
    }

    if (domid_str != NULL && cpu_list != NULL && gang_sched_pol_str != NULL) {
        cmd = CMD_SET_PARAMS_OF_DOMAIN;
        cpus = get_cpus(cpu_list, &num_of_cpus);
        get_gang_sched_policy(gang_sched_pol_str, &gang_sched_pol);

        gs_params->num_dom_entries = 1;
        gs_params->dom_entries[0].domid = *domid;
        gs_params->dom_entries[0].cpus = cpus;;
        gs_params->dom_entries[0].num_of_cpus = num_of_cpus;
        gs_params->dom_entries[0].gang_sched_policy = gang_sched_pol;    


        if (GSC_VERBOSE) {
            if (cmd == CMD_SET_PARAMS_OF_DOMAIN) {    
                printf("cpus = ");
                for (int i = 0; i < num_of_cpus; i++) {
                    printf("%d ", cpus[i]);    
                }
                printf("\n");    

                printf("Multiplexing Policy: %s (%d)\n",
                       GANG_SCHED_POLICY_2_STR(gang_sched_pol.type), 
                       gang_sched_pol.type);
            }
        }
    }
    else if (domid_str != NULL && cpu_list == NULL && gang_sched_pol_str == NULL) {
        cmd = CMD_GET_PARAMS_OF_DOMAIN;
    }
    else if (domid_str == NULL && cpu_list == NULL && gang_sched_pol_str == NULL) {
        cmd = CMD_GET_PARAMS_OF_ALL_DOMAINS;
    }
    else {
        fprintf(stderr, "ERROR: Invalid list of input parameters.\n");
        exit(EXIT_FAILURE);
    }

    return cmd;
}


/** 
 * Main function. 
 */
int main(int argc, char** argv) {
    
    xentoollog_logger* lg;
    int rc = 0;

    int cmd = CMD_INVALID;
    int cpupoolid = -1; 
    int domid = -1; 
    gang_sched_params_t dom_params = { 0 };

    cmd = process_args(argc, argv, &cpupoolid, &domid, &dom_params);


    logger = xtl_createlogger_stdiostream(stderr, minmsglevel, 0);
    if (!logger) { 
        fprintf(stderr, "Couldn't create the logger.\n");
        exit(EXIT_FAILURE);
    }

    lg = (xentoollog_logger*) logger;

    xch = xc_interface_open(lg, lg, 0);
    if (!xch) {
        fprintf(stderr, "Couldn't open the Xen control interface.\n");
        exit(EXIT_FAILURE);
    }



    if (cmd == CMD_SET_PARAMS_OF_DOMAIN) {    
        rc = gs_params_set(xch, cpupoolid, &dom_params);
        if (rc != 0) {
            fprintf(stderr, "gs_params_set(...) failed! Error code: %d\n", rc);
            exit(EXIT_FAILURE);
        }
    }
    else if (cmd == CMD_GET_PARAMS_OF_ALL_DOMAINS) {

        gang_sched_params_t* tmp_params;

        rc = gs_params_get(xch, cpupoolid, &tmp_params);
        if (rc != 0) {
            fprintf(stderr, "gs_params_get(...) failed! Error code: %d\n", rc);
            exit(EXIT_FAILURE);
        }

        print_gang_sched_params(tmp_params);

        free_gang_sched_params(tmp_params);
    }
    else if (cmd == CMD_GET_PARAMS_OF_DOMAIN) {
        // TODO: Implement this branch. 
        // Variable domid is used to select the domain's param we want to print.

        fprintf(stderr, "Operation not implemented yet!\n");
    }
    else {
        // Shouldn't happen because process_args(...) is supposed to always
        // return a valid command. 
        fprintf(stderr, "Invalid command!\n");
    }


    if (xc_interface_close(xch) != 0) {
        fprintf(stderr, "Couldn't close the Xen control interface.\n");
    }

    xtl_logger_destroy(lg);  

    return 0;
}



