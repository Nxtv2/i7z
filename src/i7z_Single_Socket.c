//i7z_Single_Socket.c
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2010 Abhishek Jaiantilal
 *
 *   Under GPL v2
 *
 * ----------------------------------------------------------------------- */
#include <memory.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <ncurses.h>
#include "i7z.h"

#define U_L_I unsigned long int
#define U_L_L_I unsigned long long int
#define numCPUs_max MAX_PROCESSORS

extern int socket_0_num, socket_1_num;
extern bool E7_mp_present;
extern int numPhysicalCores, numLogicalCores;
extern double TRUE_CPU_FREQ;
int Read_Thermal_Status_CPU(int cpu_num);

extern struct program_options prog_options;
extern FILE *fp_log_file_freq;

struct timespec global_ts;

extern char* CPU_FREQUENCY_LOGGING_FILE_single;
extern char* CPU_FREQUENCY_LOGGING_FILE_dual;
extern bool use_ncurses;

int Read_Thermal_Status_CPU(int cpu_num);
float Read_Voltage_CPU(int cpu_num);
void logCpuFreq_single_ts( struct timespec*);


void print_i7z_socket_single(struct cpu_socket_info socket_0, int printw_offset, int PLATFORM_INFO_MSR,  int PLATFORM_INFO_MSR_high,  int PLATFORM_INFO_MSR_low,
                             int* online_cpus, double cpu_freq_cpuinfo,  struct timespec one_second_sleep, char TURBO_MODE,
                             char* HT_ON_str, int* kk_1, U_L_L_I * old_val_CORE, U_L_L_I * old_val_REF, U_L_L_I * old_val_C3, U_L_L_I * old_val_C6, U_L_L_I * old_val_C7,
                             U_L_L_I * old_TSC, int estimated_mhz,  U_L_L_I * new_val_CORE,  U_L_L_I * new_val_REF,  U_L_L_I * new_val_C3,
                             U_L_L_I * new_val_C6,  U_L_L_I * new_val_C7,  U_L_L_I * new_TSC,  double* _FREQ, double* _MULT, long double * C0_time, long double * C1_time,
                             long double * C3_time,    long double * C6_time, long double * C7_time, struct timeval* tvstart, struct timeval* tvstop, int *max_observed_cpu);

void print_i7z_single ();

int Single_Socket ()
{
    //zero up the file before doing anything
    if(prog_options.logging!=0){
        fp_log_file_freq = fopen(CPU_FREQUENCY_LOGGING_FILE_single,"w");
        fclose(fp_log_file_freq);
    }


    printf ("i7z DEBUG: In i7z Single_Socket()\n\r");

    if (prog_options.proc_version.sandy_bridge)
    printf ("i7z DEBUG: guessing Sandy Bridge\n\r");
    else if (prog_options.proc_version.ivy_bridge)
    printf ("i7z DEBUG: guessing Ivy Bridge\n\r");
    else if (prog_options.proc_version.haswell)
    printf ("i7z DEBUG: guessing Haswell\n\r");
    else
    printf ("i7z DEBUG: guessing Nehalem\n\r");

    print_i7z_single();
    exit (0);
    return (1);
}

void print_i7z_socket_single(struct cpu_socket_info socket_0, int printw_offset, int PLATFORM_INFO_MSR,  int PLATFORM_INFO_MSR_high,  int PLATFORM_INFO_MSR_low,
                             int* online_cpus, double cpu_freq_cpuinfo,  struct timespec one_second_sleep, char TURBO_MODE,
                             char* HT_ON_str, int* kk_1, U_L_L_I * old_val_CORE, U_L_L_I * old_val_REF, U_L_L_I * old_val_C3, U_L_L_I * old_val_C6, U_L_L_I * old_val_C7,
                             U_L_L_I * old_TSC, int estimated_mhz,  U_L_L_I * new_val_CORE,  U_L_L_I * new_val_REF,  U_L_L_I * new_val_C3,
                             U_L_L_I * new_val_C6,  U_L_L_I * new_val_C7,  U_L_L_I * new_TSC,  double* _FREQ, double* _MULT, long double * C0_time, long double * C1_time,
                             long double * C3_time,    long double * C6_time, long double * C7_time, struct timeval* tvstart, struct timeval* tvstop, int *max_observed_cpu)
{
    int numPhysicalCores, numLogicalCores;
    double TRUE_CPU_FREQ;

    //Print a slew of information on the ncurses window
    mvprintw (0, 0, "Cpu speed from cpuinfo %0.2fMhz\n", cpu_freq_cpuinfo);
    mvprintw (1, 0,
              "cpuinfo might be wrong if cpufreq is enabled. To guess correctly try estimating via tsc\n");
    mvprintw (2, 0, "Linux's inbuilt cpu_khz code emulated now\n\n");


    //estimate the freq using the estimate_MHz() code that is almost mhz accurate
    cpu_freq_cpuinfo = estimate_MHz ();
    mvprintw (3, 0, "True Frequency (without accounting Turbo) %0.0f MHz\n",
              cpu_freq_cpuinfo);

    int i, ii;
    //int k;
    int CPU_NUM;
    int* core_list;
    unsigned long int IA32_MPERF, IA32_APERF;
    int CPU_Multiplier, error_indx;
    unsigned long long int CPU_CLK_UNHALTED_CORE, CPU_CLK_UNHALTED_REF, CPU_CLK_C3, CPU_CLK_C6, CPU_CLK_C1, CPU_CLK_C7;
    //current blck value
    float BLCK;

    char print_core[32];
    long double c1_time;

    //use this variable to monitor the max number of cores ever online
    *max_observed_cpu = (socket_0.max_cpu > *max_observed_cpu)? socket_0.max_cpu: *max_observed_cpu;

    int core_list_size_phy,    core_list_size_log;
    if (socket_0.max_cpu > 0) {
        //set the variable print_core to 0, use it to check if a core is online and doesnt
        //have any garbage values
        memset(print_core, 0, 6*sizeof(char));

        //We just need one CPU (we use Core-1) to figure out the multiplier and the bus clock freq.
        //multiplier doesnt automatically include turbo
        //note turbo is not guaranteed, only promised
        //So this msr will only reflect the actual multiplier, rest has to be figured out

        //Now get all the information about the socket from the structure
        CPU_NUM = socket_0.processor_num[0];
        core_list = socket_0.processor_num;
        core_list_size_phy = socket_0.num_physical_cores;
        core_list_size_log = socket_0.num_logical_cores;

        /*if (CPU_NUM == -1)
        {
              sleep (1);        //sleep for a bit hoping that the offline socket becomes online
              continue;
        }*/
        //number of CPUs is as told via cpuinfo
        int numCPUs = socket_0.num_physical_cores;

        CPU_Multiplier = get_msr_value (CPU_NUM, PLATFORM_INFO_MSR, PLATFORM_INFO_MSR_high, PLATFORM_INFO_MSR_low, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);

        //Blck is basically the true speed divided by the multiplier
        BLCK = cpu_freq_cpuinfo / CPU_Multiplier;

        //Use Core-1 as the one to check for the turbo limit
        //Core number shouldnt matter

        //bits from 0-63 in this store the various maximum turbo limits
        int MSR_TURBO_RATIO_LIMIT = 429;
        // 3B defines till Max 4 Core and the rest bit values from 32:63 were reserved.
        int MAX_TURBO_1C=0, MAX_TURBO_2C=0, MAX_TURBO_3C=0,
            MAX_TURBO_4C=0, MAX_TURBO_5C=0, MAX_TURBO_6C=0;

        if ( E7_mp_present){
            //e7 mp dont have 429 register so dont read the register.
        } else {
            //Bits:0-7  - core1
            MAX_TURBO_1C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 7, 0, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);
            //Bits:15-8 - core2
            MAX_TURBO_2C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 15, 8, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);
            //Bits:23-16 - core3
            MAX_TURBO_3C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 23, 16, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);
            //Bits:31-24 - core4
            MAX_TURBO_4C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 31, 24, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);

            //gulftown/Hexacore support
            //technically these should be the bits to get for core 5,6
            //Bits:39-32 - core4
            MAX_TURBO_5C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 39, 32, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);
            //Bits:47-40 - core4
            MAX_TURBO_6C = get_msr_value (CPU_NUM, MSR_TURBO_RATIO_LIMIT, 47, 40, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[0],-1);
        }
        //fflush (stdout);
        //sleep (1);

        char string_ptr1[200], string_ptr2[200];

        int IA32_PERF_GLOBAL_CTRL = 911;    //38F
        //int IA32_PERF_GLOBAL_CTRL_Value =    get_msr_value (CPU_NUM, IA32_PERF_GLOBAL_CTRL, 63, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        int IA32_FIXED_CTR_CTL = 909;    //38D
        //int IA32_FIXED_CTR_CTL_Value = get_msr_value (CPU_NUM, IA32_FIXED_CTR_CTL, 63, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);


        IA32_MPERF = get_msr_value (CPU_NUM, 231, 7, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        IA32_APERF = get_msr_value (CPU_NUM, 232, 7, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        CPU_CLK_UNHALTED_CORE = get_msr_value (CPU_NUM, 778, 63, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        CPU_CLK_UNHALTED_REF = get_msr_value (CPU_NUM, 779, 63, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        //SLEEP FOR 1 SECOND (500ms is also alright)
        nanosleep (&one_second_sleep, NULL);
        IA32_MPERF = get_msr_value (CPU_NUM, 231, 7, 0, &error_indx) - IA32_MPERF;
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        IA32_APERF = get_msr_value (CPU_NUM, 232, 7, 0, &error_indx) - IA32_APERF;
        SET_IF_TRUE(error_indx,online_cpus[0],-1);
        RETURN_IF_TRUE(online_cpus[0]==-1);

        mvprintw (4 + printw_offset, 0,"  CPU Multiplier %dx || Bus clock frequency (BCLK) %0.2f MHz \n",    CPU_Multiplier, BLCK);

        if (numCPUs <= 0) {
            sprintf (string_ptr1, "  Max TURBO Multiplier (if Enabled) with 0 cores is");
            sprintf (string_ptr2, " %dx/%dx ", MAX_TURBO_1C, MAX_TURBO_2C);
        }
        if (numCPUs >= 1 && numCPUs < 4) {
            sprintf (string_ptr1, "  Max TURBO Multiplier (if Enabled) with 1/2 Cores is");
            sprintf (string_ptr2, "  ");
        }
        if (numCPUs >= 2 && numCPUs < 6) {
            sprintf (string_ptr1, "  Max TURBO Multiplier (if Enabled) with 1/2/3/4 Cores is");
            sprintf (string_ptr2, " %dx/%dx/%dx/%dx ", MAX_TURBO_1C, MAX_TURBO_2C, MAX_TURBO_3C, MAX_TURBO_4C);
        }
        if (numCPUs >= 2 && numCPUs >= 6) {    // Gulftown 6-cores, Nehalem-EX
            sprintf (string_ptr1, "  Max TURBO Multiplier (if Enabled) with 1/2/3/4/5/6 Cores is");
            sprintf (string_ptr2, " %dx/%dx/%dx/%dx/%dx/%dx ", MAX_TURBO_1C, MAX_TURBO_2C, MAX_TURBO_3C, MAX_TURBO_4C,
                     MAX_TURBO_5C, MAX_TURBO_6C);
        }

        numCPUs = core_list_size_phy;
        numPhysicalCores = core_list_size_phy;
        numLogicalCores = core_list_size_log;

        //if (socket_0.socket_num == 0) {
            mvprintw (19, 0, "C0 = Processor running without halting ");
            mvprintw (20, 0, "C1 = Processor running with halts (States >C0 are power saver modes with cores idling)");
            mvprintw (21, 0, "C3 = Cores running with PLL turned off and core cache turned off");
            mvprintw (22, 0, "C6, C7 = Everything in C3 + core state saved to last level cache, C7 is deeper than C6");
            mvprintw (23, 0, "  Above values in table are in percentage over the last 1 sec");
//            mvprintw (24, 0, "Total Logical Cores: [%d], Total Physical Cores: [%d] \n",    numLogicalCores, numPhysicalCores);
            mvprintw (24, 0, "[core-id] refers to core-id number in /proc/cpuinfo");
            mvprintw (25, 0, "'Garbage Values' message printed when garbage values are read");
            mvprintw (26, 0, "  Ctrl+C to exit");
        //}

        mvprintw (6 + printw_offset, 0, "Socket [%d] - [physical cores=%d, logical cores=%d, max online cores ever=%d] \n", socket_0.socket_num, numPhysicalCores, numLogicalCores,*max_observed_cpu);

        mvprintw (9 + printw_offset, 0, "%s %s\n", string_ptr1, string_ptr2);

        if (TURBO_MODE == 1) {
            mvprintw (7 + printw_offset, 0, "  TURBO ENABLED on %d Cores, %s\n", numPhysicalCores, HT_ON_str);
            TRUE_CPU_FREQ = BLCK * ((double) CPU_Multiplier + 1);
            mvprintw (8 + printw_offset, 0, "  Max Frequency without considering Turbo %0.2f MHz (%0.2f x [%d]) \n", TRUE_CPU_FREQ, BLCK, CPU_Multiplier + 1);
        } else {
            mvprintw (7 + printw_offset, 0, "  TURBO DISABLED on %d Cores, %s\n", numPhysicalCores, HT_ON_str);
            TRUE_CPU_FREQ = BLCK * ((double) CPU_Multiplier);
            mvprintw (8 + printw_offset, 0,"  Max Frequency without considering Turbo %0.2f MHz (%0.2f x [%d]) \n", TRUE_CPU_FREQ, BLCK, CPU_Multiplier);
        }

        //Primarily for 32-bit users, found that after sometimes the counters loopback, so inorder
        //to prevent loopback, reset the counters back to 0 after 10 iterations roughly 10 secs
        if (*kk_1 > 10) {
            *kk_1 = 0;
            for (i = 0; i <  numCPUs; i++) {
                //Set up the performance counters and then start reading from them
                assert(i < MAX_SK_PROCESSORS);
                CPU_NUM = core_list[i];
                ii = core_list[i];
                assert(i < MAX_PROCESSORS); //online_cpus[i]
                assert(ii < numCPUs_max);

                //IA32_PERF_GLOBAL_CTRL_Value =    get_msr_value (CPU_NUM, IA32_PERF_GLOBAL_CTRL, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                set_msr_value (CPU_NUM, IA32_PERF_GLOBAL_CTRL, 0x700000003LLU);

                //IA32_FIXED_CTR_CTL_Value = get_msr_value (CPU_NUM, IA32_FIXED_CTR_CTL, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                set_msr_value (CPU_NUM, IA32_FIXED_CTR_CTL, 819);

                //IA32_PERF_GLOBAL_CTRL_Value =    get_msr_value (CPU_NUM, IA32_PERF_GLOBAL_CTRL, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                //IA32_FIXED_CTR_CTL_Value = get_msr_value (CPU_NUM, IA32_FIXED_CTR_CTL, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                old_val_CORE[ii] = get_msr_value (CPU_NUM, 778, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                old_val_REF[ii] = get_msr_value (CPU_NUM, 779, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                old_val_C3[ii] = get_msr_value (CPU_NUM, 1020, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

                old_val_C6[ii] = get_msr_value (CPU_NUM, 1021, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);

        if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
            //table b-20 in 325384 and only for sandy bridge
                old_val_C7[ii] = get_msr_value (CPU_NUM, 1022, 63, 0, &error_indx);
                SET_IF_TRUE(error_indx,online_cpus[i],-1);
                CONTINUE_IF_TRUE(online_cpus[i]==-1);
        }

                old_TSC[ii] = rdtsc ();
            }
        }
        (*kk_1)++;
        nanosleep (&one_second_sleep, NULL);
    if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
            mvprintw (11 + printw_offset, 0, "\tCore [core-id]  :Actual Freq (Mult.)\t  C0%%   Halt(C1)%%  C3 %%   C6 %%   C7 %%  Temp      VCore\n");
    }else{
            mvprintw (11 + printw_offset, 0, "\tCore [core-id]  :Actual Freq (Mult.)\t  C0%%   Halt(C1)%%  C3 %%   C6 %%  Temp      VCore\n");
    }
        //estimate the CPU speed
        estimated_mhz = estimate_MHz ();

        for (i = 0; i <  numCPUs; i++) {
            //read from the performance counters
            //things like halted unhalted core cycles

            assert(i < MAX_SK_PROCESSORS);
            CPU_NUM = core_list[i];
            ii = core_list[i];
            assert(i < MAX_PROCESSORS); //online_cpus[i]
            assert(ii < numCPUs_max);

            new_val_CORE[ii] = get_msr_value (CPU_NUM, 778, 63, 0, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[i],-1);
            CONTINUE_IF_TRUE(online_cpus[i]==-1);

            new_val_REF[ii] = get_msr_value (CPU_NUM, 779, 63, 0, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[i],-1);
            CONTINUE_IF_TRUE(online_cpus[i]==-1);

            new_val_C3[ii] = get_msr_value (CPU_NUM, 1020, 63, 0, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[i],-1);
            CONTINUE_IF_TRUE(online_cpus[i]==-1);

            new_val_C6[ii] = get_msr_value (CPU_NUM, 1021, 63, 0, &error_indx);
            SET_IF_TRUE(error_indx,online_cpus[i],-1);
            CONTINUE_IF_TRUE(online_cpus[i]==-1);

        if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
        new_val_C7[ii] = get_msr_value (CPU_NUM, 1022, 63, 0, &error_indx);
        SET_IF_TRUE(error_indx,online_cpus[i],-1);
            CONTINUE_IF_TRUE(online_cpus[i]==-1);
        }

        new_TSC[ii] = rdtsc ();

            if (old_val_CORE[ii] > new_val_CORE[ii]) {            //handle overflow
                CPU_CLK_UNHALTED_CORE = (UINT64_MAX - old_val_CORE[ii]) + new_val_CORE[ii];
            } else {
                CPU_CLK_UNHALTED_CORE = new_val_CORE[ii] - old_val_CORE[ii];
            }

            //number of TSC cycles while its in halted state
            if ((new_TSC[ii] - old_TSC[ii]) < CPU_CLK_UNHALTED_CORE) {
                CPU_CLK_C1 = 0;
            } else {
                CPU_CLK_C1 = ((new_TSC[ii] - old_TSC[ii]) - CPU_CLK_UNHALTED_CORE);
            }

            if (old_val_REF[ii] > new_val_REF[ii]) {            //handle overflow
                CPU_CLK_UNHALTED_REF = (UINT64_MAX - old_val_REF[ii]) + new_val_REF[ii];    //3.40282366921e38
            } else {
                CPU_CLK_UNHALTED_REF = new_val_REF[ii] - old_val_REF[ii];
            }

            if (old_val_C3[ii] > new_val_C3[ii]) {            //handle overflow
                CPU_CLK_C3 = (UINT64_MAX - old_val_C3[ii]) + new_val_C3[ii];
            } else {
                CPU_CLK_C3 = new_val_C3[ii] - old_val_C3[ii];
            }

            if (old_val_C6[ii] > new_val_C6[ii]) {            //handle overflow
                CPU_CLK_C6 = (UINT64_MAX - old_val_C6[ii]) + new_val_C6[ii];
            } else {
                CPU_CLK_C6 = new_val_C6[ii] - old_val_C6[ii];
            }

        if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
            if (old_val_C7[ii] > new_val_C7[ii]) {            //handle overflow
                    CPU_CLK_C7 = (UINT64_MAX - old_val_C7[ii]) + new_val_C7[ii];
                } else {
                    CPU_CLK_C7 = new_val_C7[ii] - old_val_C7[ii];
                }
        }

            _FREQ[ii] =
                THRESHOLD_BETWEEN_0_6000(estimated_mhz * ((long double) CPU_CLK_UNHALTED_CORE /
                                         (long double) CPU_CLK_UNHALTED_REF));
            _MULT[ii] = _FREQ[ii] / BLCK;

            C0_time[ii] = ((long double) CPU_CLK_UNHALTED_REF /
                           (long double) (new_TSC[ii] - old_TSC[ii]));
            C1_time[ii] = ((long double) CPU_CLK_C1 /
                           (long double) (new_TSC[ii] - old_TSC[ii]));
            C3_time[ii] = ((long double) CPU_CLK_C3 /
                           (long double) (new_TSC[ii] - old_TSC[ii]));
            C6_time[ii] = ((long double) CPU_CLK_C6 /
                           (long double) (new_TSC[ii] - old_TSC[ii]));

        if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
            C7_time[ii] = ((long double) CPU_CLK_C7 /
                               (long double) (new_TSC[ii] - old_TSC[ii]));
        }

        if (C0_time[ii] < 1e-2) {
                if (C0_time[ii] > 1e-4) {
                    C0_time[ii] = 0.01;
                } else {
                    C0_time[ii] = 0;
                }
            }
            if (C1_time[ii] < 1e-2) {
                if (C1_time[ii] > 1e-4) {
                    C1_time[ii] = 0.01;
                } else {
                    C1_time[ii] = 0;
                }
            }
            if (C3_time[ii] < 1e-2) {
                if (C3_time[ii] > 1e-4) {
                    C3_time[ii] = 0.01;
                } else {
                    C3_time[ii] = 0;
                }
            }
            if (C6_time[ii] < 1e-2) {
                if (C6_time[ii] > 1e-4) {
                    C6_time[ii] = 0.01;
                } else {
                    C6_time[ii] = 0;
                }
            }
        if(!prog_options.proc_version.nehalem){
            if (C7_time[ii] < 1e-2) {
                   if (C7_time[ii] > 1e-4) {
                       C7_time[ii] = 0.01;
                   } else {
                       C7_time[ii] = 0;
                   }
                }
        }
    }
        //CHECK IF ALL COUNTERS ARE CORRECT AND NO GARBAGE VALUES ARE PRESENT
        //If there is any garbage values set print_core[i] to 0
        for (ii = 0; ii <  numCPUs; ii++) {
            assert(ii < MAX_SK_PROCESSORS);
            i = core_list[ii];
        if(!prog_options.proc_version.nehalem){
          if ( !IS_THIS_BETWEEN_0_100(C0_time[i] * 100) ||
                    !IS_THIS_BETWEEN_0_100(C1_time[i] * 100 - (C3_time[i] + C6_time[i]) * 100) ||
                    !IS_THIS_BETWEEN_0_100(C3_time[i] * 100) ||
                    !IS_THIS_BETWEEN_0_100(C6_time[i] * 100) ||
                    !IS_THIS_BETWEEN_0_100(C7_time[i] * 100) ||
                    isinf(_FREQ[i]) )
                  print_core[ii]=0;
              else
                  print_core[ii]=1;
        }else{
          if ( !IS_THIS_BETWEEN_0_100(C0_time[i] * 100) ||
                    !IS_THIS_BETWEEN_0_100(C1_time[i] * 100 - (C3_time[i] + C6_time[i]) * 100) ||
                    !IS_THIS_BETWEEN_0_100(C3_time[i] * 100) ||
                    !IS_THIS_BETWEEN_0_100(C6_time[i] * 100) ||
                    isinf(_FREQ[i]) )
                  print_core[ii]=0;
              else
                  print_core[ii]=1;
        }
        }

        //Now print the information about the cores. Print garbage values message if there is garbage
        for (ii = 0; ii <  numCPUs; ii++) {
            assert(ii < MAX_SK_PROCESSORS);
            i = core_list[ii];

        if(!prog_options.proc_version.nehalem){
            //there is a bit of leeway to be had as the total counts might deviate
            //if this happens c1_time might be negative so just adjust so that it is thresholded to 0
            c1_time = C1_time[i] * 100 - (C3_time[i] + C6_time[i] + C7_time[i]) * 100;
            if (!isnan(c1_time) && !isinf(c1_time)) {
                    if (c1_time <= 0) {
            c1_time=0;
                    }
            }
            if (print_core[ii])
                    mvprintw (12 + ii + printw_offset, 0, "\tCore %d [%d]:\t  %0.2f (%.2fx)\t%4.3Lg\t%4.3Lg\t%4.3Lg\t%4.3Lg\t%4.3Lg\t%d\t%0.4f\n",
                          ii + 1, core_list[ii], _FREQ[i], _MULT[i], THRESHOLD_BETWEEN_0_100(C0_time[i] * 100),
                          THRESHOLD_BETWEEN_0_100(c1_time), THRESHOLD_BETWEEN_0_100(C3_time[i] * 100), THRESHOLD_BETWEEN_0_100(C6_time[i] * 100),THRESHOLD_BETWEEN_0_100(C7_time[i] * 100),
              Read_Thermal_Status_CPU(core_list[ii]),    //C0_time[i]*100+C1_time[i]*100 around 100
                          Read_Voltage_CPU(core_list[ii]));
            else
                    mvprintw (12 + ii + printw_offset, 0, "\tCore %d [%d]:\t  Garbage Values\n", ii + 1, core_list[ii]);
        }else{
            //there is a bit of leeway to be had as the total counts might deviate
            //if this happens c1_time might be negative so just adjust so that it is thresholded to 0
            c1_time = C1_time[i] * 100 - (C3_time[i] + C6_time[i]) * 100;
            if (!isnan(c1_time) && !isinf(c1_time)) {
                    if (c1_time <= 0) {
            c1_time=0;
                    }
            }
            if (print_core[ii])
                    mvprintw (12 + ii + printw_offset, 0, "\tCore %d [%d]:\t  %0.2f (%.2fx)\t%4.3Lg\t%4.3Lg\t%4.3Lg\t%4.3Lg\t%d\t%0.4f\n",
                          ii + 1, core_list[ii], _FREQ[i], _MULT[i], THRESHOLD_BETWEEN_0_100(C0_time[i] * 100),
                          THRESHOLD_BETWEEN_0_100(c1_time), THRESHOLD_BETWEEN_0_100(C3_time[i] * 100), THRESHOLD_BETWEEN_0_100(C6_time[i] * 100),Read_Thermal_Status_CPU(core_list[ii]),    //C0_time[i]*100+C1_time[i]*100 around 100
                          Read_Voltage_CPU(core_list[ii]));
            else
                    mvprintw (12 + ii + printw_offset, 0, "\tCore %d [%d]:\t  Garbage Values\n", ii + 1, core_list[ii]);
        }
        }

        /*k=0;
        for (ii = 00; ii < *max_observed_cpu; ii++)
        {
                  if (in_core_list(ii,core_list)){
                      continue;
                  }else{
                      mvprintw (12 + k + numCPUs + printw_offset, 0, "\tProcessor %d [%d]:  OFFLINE\n", k + numCPUs + 1, ii);
                  }
                  k++;
        }*/

        //FOR THE REST OF THE CORES (i.e. the offline cores+non-present cores=6 )
        //I have space allocated for 6 cores to be printed per socket so from all the present cores
        //till 6 print a blank line

        //for(ii=*max_observed_cpu; ii<6; ii++)
        for (ii = numCPUs; ii<6; ii++)
            mvprintw (12 + ii + printw_offset, 0, "\n");

        TRUE_CPU_FREQ = 0;

        logOpenFile_single();

        //time_t time_to_save;
        //logCpuFreq_single_d(time(&time_to_save));
        clock_gettime(CLOCK_REALTIME, &global_ts);
        logCpuFreq_single_ts( &global_ts);

        logCpuCstates_single_ts( &global_ts);

        for (ii = 0; ii < numCPUs; ii++) {
            assert(ii < MAX_SK_PROCESSORS);
            i = core_list[ii];
            if ( (_FREQ[i] > TRUE_CPU_FREQ) && (print_core[ii]) && !isinf(_FREQ[i]) ) {
                TRUE_CPU_FREQ = _FREQ[i];
            }
            if ( (print_core[ii]) && !isinf(_FREQ[i]) ) {
                logCpuFreq_single(_FREQ[i]);
            }
            logCpuCstates_single_c(" [");
            logCpuCstates_single((float)THRESHOLD_BETWEEN_0_100(C0_time[i] * 100));  logCpuCstates_single_c(",");
            c1_time = C1_time[i] * 100 - (C3_time[i] + C6_time[i] + C7_time[i]) * 100;
            logCpuCstates_single((float)THRESHOLD_BETWEEN_0_100(c1_time));           logCpuCstates_single_c(",");
            logCpuCstates_single((float)THRESHOLD_BETWEEN_0_100(C3_time[i] * 100));  logCpuCstates_single_c(",");
            logCpuCstates_single((float)THRESHOLD_BETWEEN_0_100(C6_time[i] * 100));
            if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
                logCpuCstates_single_c(",");
                logCpuCstates_single((float)THRESHOLD_BETWEEN_0_100(C7_time[i] * 100));
            }
            logCpuCstates_single_c("]\t");
        }
        //        logCpuCstates_single_c("\n");
        logCloseFile_single();

        mvprintw (10 + printw_offset, 0,
                  "  Real Current Frequency %0.2f MHz [%0.2f x %0.2f] (Max of below)\n", TRUE_CPU_FREQ, BLCK, TRUE_CPU_FREQ/BLCK);

        refresh ();

        //shift the new values to the old counter values
        //so that the next time we use those to find the difference
        memcpy (old_val_CORE, new_val_CORE,
                sizeof (*old_val_CORE) * numCPUs);
        memcpy (old_val_REF, new_val_REF, sizeof (*old_val_REF) * numCPUs);
        memcpy (old_val_C3, new_val_C3, sizeof (*old_val_C3) * numCPUs);
        memcpy (old_val_C6, new_val_C6, sizeof (*old_val_C6) * numCPUs);

    if(prog_options.proc_version.sandy_bridge || prog_options.proc_version.ivy_bridge || prog_options.proc_version.haswell){
        memcpy (old_val_C7, new_val_C7, sizeof (*old_val_C7) * numCPUs);
    }

        memcpy (tvstart, tvstop, sizeof (*tvstart) * numCPUs);
        memcpy (old_TSC, new_TSC, sizeof (*old_TSC) * numCPUs);
    } else {
        // If all the cores in the socket go offline, just erase the whole screen
        //WELL for single socket machine this code will never be executed. lol
        //atleast 1 core will be online so ...
        //for (ii = 0 ; ii<14; ii++)
        //    mvprintw (3 + ii + printw_offset, 0, "Ending up here\n");
        //print_socket_information(&socket_0);
    }

}

void print_i7z_single ()
{
    struct cpu_hierarchy_info chi;
    struct cpu_socket_info socket_0={.max_cpu=0, .socket_num=0, .processor_num={-1,-1,-1,-1,-1,-1,-1,-1}};
    struct cpu_socket_info socket_1={.max_cpu=0, .socket_num=1, .processor_num={-1,-1,-1,-1,-1,-1,-1,-1}};

    construct_CPU_Hierarchy_info(&chi);
    construct_sibling_list(&chi);
//      print_CPU_Hierarchy(chi);
    construct_socket_information(&chi, &socket_0, &socket_1, socket_0_num, socket_1_num);
//      print_socket_information(&socket_0);
//      print_socket_information(&socket_1);

    int printw_offset = (0) * 14;

    //turbo_mode enabled/disabled flag
    char TURBO_MODE;

    double cpu_freq_cpuinfo;

    cpu_freq_cpuinfo = cpufreq_info ();
    //estimate the freq using the estimate_MHz() code that is almost mhz accurate
    cpu_freq_cpuinfo = estimate_MHz ();

    //Print a slew of information on the ncurses window
    //I already print that in the loop so..
    //mvprintw (0, 0, "WAIT .... ");



    //MSR number and hi:low bit of that MSR
    //This msr contains a lot of stuff, per socket wise
    //one can pass any core number and then get in multiplier etc
    int PLATFORM_INFO_MSR = 206;    //CE 15:8
    int PLATFORM_INFO_MSR_low = 8;
    int PLATFORM_INFO_MSR_high = 15;

    unsigned long long int old_val_CORE[2][numCPUs_max], new_val_CORE[2][numCPUs_max];
    unsigned long long int old_val_REF[2][numCPUs_max], new_val_REF[2][numCPUs_max];
    unsigned long long int old_val_C3[2][numCPUs_max], new_val_C3[2][numCPUs_max];
    unsigned long long int old_val_C6[2][numCPUs_max], new_val_C6[2][numCPUs_max];
    unsigned long long int old_val_C7[2][numCPUs_max], new_val_C7[2][numCPUs_max];

    unsigned long long int old_TSC[2][numCPUs_max], new_TSC[2][numCPUs_max];
    long double C0_time[2][numCPUs_max], C1_time[2][numCPUs_max],
    C3_time[2][numCPUs_max], C6_time[2][numCPUs_max], C7_time[2][numCPUs_max];
    double _FREQ[2][numCPUs_max], _MULT[2][numCPUs_max];
    struct timeval tvstart[2][numCPUs_max], tvstop[2][numCPUs_max];

    struct timespec one_second_sleep;
    one_second_sleep.tv_sec = 0;
    one_second_sleep.tv_nsec = 499999999;    // 500msec



    //Get turbo mode status by reading msr within turbo_status
    TURBO_MODE = turbo_status ();

    //Flags and other things about HT.
    char HT_ON_str[30];

    int kk_1 = 11;

    //below variables is used to monitor if any cores went offline etc.
    int online_cpus[MAX_PROCESSORS]; //Max 2 x Nehalem-EX with total 32 threads

    double estimated_mhz=0;
    int socket_num;

    //below variables stores how many cpus were observed till date for the socket
    int max_cpus_observed=0;

    //Setup stuff for ncurses
    if (prog_options.use_ncurses) {
        init_ncurses();
        initscr ();
        start_color ();
    } else {
        printf("GUI has been turned OFF\n");
    }
    
    //estimate the freq using the estimate_MHz() code that is almost mhz accurate
    cpu_freq_cpuinfo = estimate_MHz ();
    mvprintw (3, 0, "True Frequency (without accounting Turbo) %0.0f MHz\n", cpu_freq_cpuinfo);
    
    for (;;) {
        construct_CPU_Hierarchy_info(&chi);
        construct_sibling_list(&chi);
        construct_socket_information(&chi, &socket_0, &socket_1, socket_0_num, socket_1_num);
        
        //HT enabled if num logical > num physical cores
        if (chi.HT==1) {
            strncpy (HT_ON_str, "Hyper Threading ON\0", 30);
        } else {
            strncpy (HT_ON_str, "Hyper Threading OFF\0", 30);
        }

        SET_ONLINE_ARRAY_PLUS1(online_cpus)

        //In the function calls below socket_num is set to the socket to print for
        //printw_offset is the offset gap between the printing of the two sockets
        //kk_1 and kk_2 are the variables that have to be set, i have to use them internally
        //so in future if there are more sockets to be printed, add more kk_*
        socket_num=0;
        printw_offset=0;

        //printf("socket0 max cpu %d\n",socket_0.max_cpu);
        //printf("socket1 max cpu %d\n",socket_0.max_cpu);


        //below code in (else case) is to handle when for 2 sockets system, cpu1 is populated and cpu0 is empty.
        //single socket code but in an intelligent manner and not assuming that cpu0 is always populated before cpu1
        if(socket_0.max_cpu>1){
            socket_num=0;
            print_i7z_socket_single(socket_0, printw_offset, PLATFORM_INFO_MSR,  PLATFORM_INFO_MSR_high, PLATFORM_INFO_MSR_low,
                                online_cpus, cpu_freq_cpuinfo, one_second_sleep, TURBO_MODE, HT_ON_str, &kk_1, old_val_CORE[socket_num],
                                old_val_REF[socket_num], old_val_C3[socket_num], old_val_C6[socket_num],old_val_C7[socket_num],
                                old_TSC[socket_num], estimated_mhz, new_val_CORE[socket_num], new_val_REF[socket_num], new_val_C3[socket_num],
                                new_val_C6[socket_num],new_val_C7[socket_num], new_TSC[socket_num], _FREQ[socket_num], _MULT[socket_num], C0_time[socket_num], C1_time[socket_num],
                                C3_time[socket_num], C6_time[socket_num],C7_time[socket_num], tvstart[socket_num], tvstop[socket_num], &max_cpus_observed);
        } else {
            socket_num=1;
            print_i7z_socket_single(socket_1, printw_offset, PLATFORM_INFO_MSR,  PLATFORM_INFO_MSR_high, PLATFORM_INFO_MSR_low,
                            online_cpus, cpu_freq_cpuinfo, one_second_sleep, TURBO_MODE, HT_ON_str, &kk_1, old_val_CORE[socket_num],
                                    old_val_REF[socket_num], old_val_C3[socket_num], old_val_C6[socket_num],old_val_C7[socket_num],
                                    old_TSC[socket_num], estimated_mhz, new_val_CORE[socket_num], new_val_REF[socket_num], new_val_C3[socket_num],
                                    new_val_C6[socket_num],new_val_C7[socket_num], new_TSC[socket_num], _FREQ[socket_num], _MULT[socket_num], C0_time[socket_num], C1_time[socket_num],
                                    C3_time[socket_num], C6_time[socket_num],C7_time[socket_num], tvstart[socket_num], tvstop[socket_num], &max_cpus_observed);
        }
    }
}
