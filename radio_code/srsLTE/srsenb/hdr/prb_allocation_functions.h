#ifndef SRSLTE_PRB_ALLOCATION_FUNCTIONS_H
#define SRSLTE_PRB_ALLOCATION_FUNCTIONS_H

#include <map>
#include <inttypes.h>
#include <srsenb/hdr/stack/mac/scheduler_ue.h>

#include "srslte/interfaces/enb_interfaces.h"

std::map<uint16_t, uint32_t> build_user_prb_map(std::map<uint16_t, srsenb::sched_ue> &ue_db, uint32_t tti, uint32_t prb_max);
uint32_t get_tbs_dl(int mcs, uint32_t nb_rb);
uint32_t get_i_tbs(int i_mcs);
std::map<uint16_t, uint32_t> compute_waterfilling_allocation(std::map<uint16_t, uint32_t> rnti_prb_req, uint32_t prb_max);
std::map<uint16_t, uint32_t> compute_proportional_allocation(std::map<uint16_t, uint32_t > rnti_prb_req, uint32_t prb_max);

#endif //SRSLTE_PRB_ALLOCATION_FUNCTIONS_H