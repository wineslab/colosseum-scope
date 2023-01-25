// This file holds global variables and constants used by SCOPE

#ifndef SRSLTE_GLOBAL_VARIABLES_H
#define SRSLTE_GLOBAL_VARIABLES_H

#include <stdint.h>

// max number of possible tenants for network slicing
#define MAX_SLICING_TENANTS 10
#define MAX_MASK_LENGTH 25      // same as MAX_RBG defined by srsLTE

// first valid user RNTI (before it was hardcoded to 70 in srsLTE)
#define FIRST_VALID_USER_RNTI 70

// max user RNTI is the limit of the uint16_t type used to store RNTIs
#define MAX_USER_RNTI 65535 - 1

// path for SCOPE configuration files
#define SCOPE_CONFIG_DIR "/root/radio_code/scope_config/"

// save tenant slicing masks
typedef struct {
    int slice_id;

    // total number of PRBs in this slice
    int slice_prbs;

    // scheduling policy for this slice
    // 0 = default srsLTE round-robin
    // 1 = waterfilling
    int scheduling_policy;

    // slicing allocation mask
    uint8_t slicing_mask[MAX_MASK_LENGTH];

    // slicing mask for current tti
    uint8_t tti_slicing_mask[MAX_MASK_LENGTH];

    // UL slicing allocation mask
    uint8_t ul_slicing_mask[MAX_MASK_LENGTH];

    // UL slicing mask for current tti
    uint8_t ul_tti_slicing_mask[MAX_MASK_LENGTH];

} Slice_Tenants;

// add structure to save user parameters
struct users_resources {
    // record imsi and whether imsi has already been acquired
    long long unsigned int imsi;
    int imsi_acquired;

    // record tmsi
    uint32_t tmsi;

    // record slice_id and whether slice_id has already been acquired
    int slice_id;
    int slice_id_acquired;

    // power multiplier used for this user
    float power_multiplier;
    long int timestamp_power_multiplier_read;

    // downlink SINR
    double dl_sinr;

    // record number of  assigned (requested) and granted prbs
    int sum_assigned_prbs;
    int sum_granted_prbs;

    // fix modulations to use, ignored if 0
    int dl_modulation;
    int ul_modulation;
    long int timestamp_forced_modulation_read;
};

// save slicing allocation mask of tenants
// index in array also corresponds to slice ID
extern Slice_Tenants slicing_structure[MAX_SLICING_TENANTS];

// NOTE: use (RNTI - FIRST_VALID_USER_RNTI) as index
extern struct users_resources ue_resources[500];

// keep track whether network slicing is active or not
extern int network_slicing_enabled;

// choose whether to use srsLTE default scheduler or SCOPE scheduler
// 0 = srsLTE default round-robin
// 1 = waterfilling
// 2 = proportional
extern int global_scheduling_policy;

// number of cell PRBs
extern int cell_prbs_global;

// number of cell Resource Block Groups (RBGs)
extern int cell_rbgs;

// threshold to wait before applying custom scheduling policies
extern int sched_threshold;

// how often throughput is logged on file
extern float metrics_period_secs;

// how often to log downlink MCS
extern int log_dl_mcs_every_ms;

// are we running on colosseum?
extern int colosseum_testbed;

// use fixed downlink/uplink modulation
extern int force_dl_modulation;
extern int force_ul_modulation;

#endif //SRSLTE_GLOBAL_VARIABLES_H
