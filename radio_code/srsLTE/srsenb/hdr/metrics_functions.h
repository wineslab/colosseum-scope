#ifndef SRSLTE_METRICS_FUNCTIONS_H
#define SRSLTE_METRICS_FUNCTIONS_H

#include "lib/include/srslte/interfaces/enb_metrics_interface.h"

#include <stdint.h>
#include <string>

double sinr_from_cqi(uint32_t tm, uint32_t cqi);
uint32_t cqi_from_modulation(int modulation);
void save_user_metric_and_log(uint16_t ue_rnti, double metric_value, std::string metric_name, std::string config_dir_path);
void log_user_metric(int ue_rnti, float metric_value, std::string metric_name, std::string config_dir_path, long int timestamp);
void save_ue_metrics_to_csv(srsenb::enb_metrics_t* m, float period_s, std::string config_dir_path);
long int get_time_milliseconds(void);

// map sinr to cqi
extern const double sinr_to_cqi[4][16];

// compute user throughput
void compute_user_throughput(int ue_rnti, int tx_bitrate, float period_s, long int timestamp);

// get number of PRBs per RBG. Relationship is as follows:
// +-----------------+------------+--------------+
// | Bandwidth (MHz) | Total PRBs | PRBs per RBG |
// +-----------------+------------+--------------+
// |             1.4 |          6 |            1 |
// |               3 |         15 |            2 |
// |               5 |         25 |            2 |
// |              10 |         50 |            3 |
// |              15 |         75 |            4 |
// |              20 |        100 |            4 |
// +-----------------+------------+--------------+
uint32_t get_prbs_per_rbg();

// get number of actually allocated PRBs
uint32_t get_granted_prbs_from_rbg(uint32_t rbg_requested, uint32_t rbg_granted, uint8_t rbg_mask_last_bit);

#endif //SRSLTE_METRICS_FUNCTIONS_H
