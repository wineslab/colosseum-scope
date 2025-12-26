/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsenb/hdr/stack/mac/scheduler_metric.h"
#include "srsenb/hdr/stack/mac/scheduler_harq.h"
#include "srslte/common/log_helper.h"
#include "srslte/common/logmap.h"
#include <string.h>

#include <srsenb/hdr/ue_imsi_functions.h>
#include <srsenb/hdr/ue_rnti_functions.h>
#include <srsenb/hdr/slicing_functions.h>
#include <srsenb/hdr/metrics_functions.h>
#include <srsenb/hdr/prb_allocation_functions.h>
#include <srsenb/hdr/global_variables.h>

#include <iostream>

int network_slicing_enabled;
int global_scheduling_policy;
int force_dl_modulation;
int force_ul_modulation;

Slice_Tenants slicing_structure[MAX_SLICING_TENANTS];

namespace srsenb {

/*****************************************************************
 *
 * Downlink Metric
 *
 *****************************************************************/

// SCOPE: add constructor to initialize variables
dl_metric_rr::dl_metric_rr() {
    // initialize variables
    slicing_line_no = 0;
    last_time_line_changed_ms = 0;
}

void dl_metric_rr::set_params(const sched_cell_params_t& cell_params_)
{
  cc_cfg = &cell_params_;
  log_h  = srslte::logmap::get("MAC ");
}

void dl_metric_rr::sched_users(std::map<uint16_t, sched_ue>& ue_db, dl_sf_sched_itf* tti_sched)
{
  long int timestamp_ms;

  int log_prb_every_ms = 250;

  // SCOPE: slice allocation mask line to read from configuration file
  int max_lines = 10;

  // SCOPE: how frequently to update slice allocation mask and scheduling policy
  int line_change_frequency_ms = 250;

  // SCOPE: parameter to read forced modulation from file
  int forced_modulation_frequency_ms = 60000;

  tti_alloc = tti_sched;

  if (ue_db.empty()) {
    return;
  }

  // SCOPE: get timestamp [ms] to be used both for slicing and PRBs
  timestamp_ms = get_time_milliseconds();

  // SCOPE: read slicing mask for the user every once in a while
  if (network_slicing_enabled) {
      if (last_time_line_changed_ms == 0 || (timestamp_ms - last_time_line_changed_ms >= line_change_frequency_ms)) {
          if (slicing_line_no >= max_lines)
              slicing_line_no = 0;

          // update timestamp
          last_time_line_changed_ms = timestamp_ms;

          // get user slice allocation mask from configuration file and save it into dl_metric_rr structure
          // NOTE: function returns without any error if slicing configuration file is not found
          for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {
              get_slicing_allocation_mask(s_idx, &slicing_structure[s_idx], slicing_line_no);

              // get scheduling policy for each slice from config file and save it in structure
              slicing_structure[s_idx].scheduling_policy = get_scheduling_policy_from_slice(s_idx);
          }

          // update line number
          slicing_line_no++;
      }

      // copy slicing mask to tti slicing mask. Do this at every new tti
      for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {
          for (int rbg_idx = 0; rbg_idx < MAX_MASK_LENGTH; ++rbg_idx) {
              slicing_structure[s_idx].tti_slicing_mask[rbg_idx] = slicing_structure[s_idx].slicing_mask[rbg_idx];
          }
      }
  }

  // SCOPE: plug-in waterfilling-like and proportoinal scheduling functions and return a vector of prbs needed by each user
  std::map<uint16_t, uint32_t> rnti_prb_req;
  std::map<uint16_t, uint32_t> rnti_prb_alloc_wf;
  std::map<uint16_t, uint32_t> rnti_prb_alloc_pr;
  if (network_slicing_enabled || (!network_slicing_enabled && global_scheduling_policy > 0)) {
      rnti_prb_req = build_user_prb_map(ue_db, tti_alloc->get_tti_tx_dl(), ue_db.begin()->second.get_serving_cell_prbs());
      rnti_prb_alloc_wf = compute_waterfilling_allocation(rnti_prb_req, ue_db.begin()->second.get_serving_cell_prbs());
      rnti_prb_alloc_pr = compute_proportional_allocation(rnti_prb_req, ue_db.begin()->second.get_serving_cell_prbs());
  }

  // give priority in a time-domain RR basis.
  uint32_t priority_idx = tti_alloc->get_tti_tx_dl() % (uint32_t)ue_db.size();

  // SCOPE: add randomness to priority_idx otherwise it is deterministic in case of two users
  priority_idx = (priority_idx + rand() % 42) % (uint32_t)ue_db.size();

  auto     iter         = ue_db.begin();
  std::advance(iter, priority_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin(); // wrap around
    }
    sched_ue* user = &iter->second;

    // SCOPE: read user IMSI from configuration file and save it into user structure
    if (user->imsi == 0) {
        // get user IMSI from structure
        int ue_array_idx = get_ue_idx_from_rnti(user->get_rnti());
        long long unsigned int ue_imsi = ue_resources[ue_array_idx].imsi;

        if (ue_imsi > 0) {
            // save user IMSI
            user->imsi = ue_imsi;
            std::cout << "UE IMSI: " << ue_imsi << std::endl;
        }
    }

    // SCOPE: build config dir path
    std::string slicing_config_dir_path = SCOPE_CONFIG_DIR;
    slicing_config_dir_path += "slicing/";

    if (user && is_user(user->get_rnti())) {
      int ue_array_idx = get_ue_idx_from_rnti(user->get_rnti());

      // SCOPE: read downlink modulation parameter
      if ((force_dl_modulation || force_ul_modulation) && user->imsi > 0) {
        if (timestamp_ms - ue_resources[ue_array_idx].timestamp_forced_modulation_read >= forced_modulation_frequency_ms) {
          ue_resources[ue_array_idx].timestamp_forced_modulation_read = timestamp_ms;

          if (force_dl_modulation) {
            // get dl modulation from file
            std::string modulation_dl_file_name  = "ue_imsi_modulation_dl.txt";
            int dl_modulation = (int) get_value_from_imsi(user->imsi, slicing_config_dir_path, modulation_dl_file_name);

            // default to 0 (not forced) if value not found in config file
            if (dl_modulation < 0) {
              dl_modulation = 0;
            }

            ue_resources[ue_array_idx].dl_modulation = dl_modulation;
            std::cout << "IMSI " << user->imsi << ", forced DL modulation to " << ue_resources[ue_array_idx].dl_modulation << std::endl;
          }

          if (force_ul_modulation) {
            // get ul modulation from file
            std::string modulation_ul_file_name  = "ue_imsi_modulation_ul.txt";
            int ul_modulation = (int) get_value_from_imsi(user->imsi, slicing_config_dir_path, modulation_ul_file_name);

            // default to 0 (not forced) if value not found in config file
            if (ul_modulation < 0) {
              ul_modulation = 0;
            }

            ue_resources[ue_array_idx].ul_modulation = ul_modulation;
            std::cout << "IMSI " << user->imsi << ", forced UL modulation to " << ue_resources[ue_array_idx].ul_modulation << std::endl;
          }
        }
      }
    }

    // SCOPE: read user slicing ownership from configuration file and save it into user structure
    if (user->slice_number == -1 && user->imsi > 0) {
        int ue_slice = -1;  // default

        if (network_slicing_enabled) {
            // get user slice ownership
            std::string slice_file_name = "ue_imsi_slice.txt";
            ue_slice = (int) get_value_from_imsi(user->imsi, slicing_config_dir_path, slice_file_name);
        }

        // force slice 0 if IMSI was not found in the configuration file
        if (ue_slice == -1) {
            ue_slice = 0;
        }

        // save user RNTI in configuration file
        user->slice_number = ue_slice;
        write_user_parameters_on_file(user->get_rnti(), ue_slice);
    }

    // SCOPE: save slicing mask in user structure
    if (network_slicing_enabled && user->slice_number > -1) {
        user->slicing_mask.set_from_array(0, user->slicing_mask.size(), slicing_structure[user->slice_number].slicing_mask);
        user->has_slicing_mask = 1;
    }

    // SCOPE: adding variable of required PRBs
    // this is used only to log if using srsLTE default scheduler and
    // also to assign if using SCOPE scheduler
    uint32_t user_prb = 0;
    uint32_t req_prb = 0;

    // SCOPE: catch exception in case there is no value for current RNTI in map.
    // It might happen in the first rounds during association
    // Enable use of srsLTE default scheduler
    if (user->times_scheduled < sched_threshold ||
        (network_slicing_enabled && slicing_structure[user->slice_number].scheduling_policy == 0) ||
        (!network_slicing_enabled && global_scheduling_policy == 0)) {
        // give some room for association. Use any number, with 'false' it is not used
        allocate_user(user, -1, false);

        // log PRBs if using srsLTE default scheduler
        if ((network_slicing_enabled && slicing_structure[user->slice_number].scheduling_policy == 0) ||
            (!network_slicing_enabled && global_scheduling_policy == 0)) {
            // get user required bytes and PRBs
            uint32_t req_bytes = user->get_pending_dl_new_data_total();

            // get number of PRBs required in DL for statistics
            auto p = user->get_cell_index(cc_cfg->enb_cc_idx);
            if (p.first) {
                uint32_t cell_idx = p.second;
                req_prb = user->get_required_prb_dl(cell_idx, req_bytes, tti_alloc->get_nof_ctrl_symbols());
            }
        }
    }
    else {
        try {
            if ((network_slicing_enabled && slicing_structure[user->slice_number].scheduling_policy == 1) ||
            (!network_slicing_enabled && global_scheduling_policy == 1)) {
                // use waterfilling allocation
                user_prb = rnti_prb_alloc_wf.at(iter->second.get_rnti());
            }
            else {
                // if neither round-robin nor waterfilling, then it is proportional allocation
                user_prb = rnti_prb_alloc_pr.at(iter->second.get_rnti());
            }

            allocate_user(user, user_prb, true);

            // get number of PRBs required in DL for statistics
            req_prb = rnti_prb_req.at(iter->second.get_rnti());
        }
        catch (std::out_of_range) {
            // user is not actually asking for data, let stsLTE handle this case
        }
    }

    // SCOPE: save requested prbs into stucture to periodically log statistics
    ue_resources[get_ue_idx_from_rnti(iter->first)].sum_assigned_prbs += req_prb;
  }
}

bool dl_metric_rr::find_allocation(uint32_t min_nof_rbg, uint32_t max_nof_rbg, rbgmask_t* rbgmask)
{
  if (tti_alloc->get_dl_mask().all()) {
    return false;
  }
  // 1's for free rbgs
  rbgmask_t localmask = ~(tti_alloc->get_dl_mask());

  uint32_t i = 0, nof_alloc = 0;
  for (; i < localmask.size() and nof_alloc < max_nof_rbg; ++i) {
    if (localmask.test(i)) {
      nof_alloc++;
    }
  }
  if (nof_alloc < min_nof_rbg) {
    return false;
  }
  localmask.fill(i, localmask.size(), false);
  *rbgmask = localmask;
  return true;
}

// SCOPE: find allocation in case of slicing
bool dl_metric_rr::find_allocation_slicing(uint32_t min_nof_rbg, uint32_t max_nof_rbg,
                                           uint8_t* tti_slicing_mask, rbgmask_t* rbgmask)
{
  uint32_t nof_alloc = 0;

  // SCOPE: check if we are transmitting control.
  // In that case it takes the first two RBGs and the current user should not transmit there
  // NOTE: tti_alloc->get_nof_ctrl_symbols() returns 2 in case of control symbols,
  // and control occupies the first two RBGs. As an alternative, the first two RBGs
  // of the localmask (rbgmask_t localmask = ~(tti_alloc->get_dl_mask());) are set to 0,
  // although this may happen even if another user is transmitting there
  bool ctrl_present = false;
  int num_ctrl_rbgs = tti_alloc->get_nof_ctrl_symbols();
  if (num_ctrl_rbgs >= 2) {
    ctrl_present = true;
  }

  // create and initialize to 0
  uint8_t mask_size = rbgmask->size();
  uint8_t tmp_user_mask[mask_size];

  for (int i = 0; i < mask_size; ++i)
    tmp_user_mask[i] = 0;

  for (int i = 0; i < mask_size && nof_alloc < max_nof_rbg; ++i) {
    // SCOPE: skip control RBG if control is present
    if (ctrl_present && i < num_ctrl_rbgs) {
        // update global mask of tenant to specify the current rbg has been taken
        tti_slicing_mask[i] = 0;
    }
    else {
        // check if RBG is free
        if (tti_slicing_mask[i] == 1) {
          nof_alloc++;

          // update global mask of tenant to specify the current rbg has been taken
          tti_slicing_mask[i] = 0;

          // update user mask to a free (1)
          tmp_user_mask[i] = 1;
        }
    }
  }

  // set rgbmask to return
  rbgmask->set_from_array(0, mask_size, tmp_user_mask);

  //if (nof_alloc < min_nof_rbg) {
  if (nof_alloc == 0) {
    return false;
  }

  return true;
}

// SCOPE: pass a couple of extra parameters for waterfilling
// prb_alloc_num: pre-computed number of PRBs for the user. Only used if prb_alloc_num is true
// fixed_allocation: true if prb_alloc_num is to be used.
dl_harq_proc* dl_metric_rr::allocate_user(sched_ue* user, uint32_t prb_num, bool fixed_allocation)
{
    // Do not allocate a user multiple times in the same tti
    if (tti_alloc->is_dl_alloc(user)) {
        return nullptr;
    }
    // Do not allocate a user to an inactive carrier
    auto p = user->get_cell_index(cc_cfg->enb_cc_idx);
    if (not p.first) {
        return nullptr;
    }
    uint32_t cell_idx = p.second;

    alloc_outcome_t code;
    uint32_t        tti_dl = tti_alloc->get_tti_tx_dl();
    dl_harq_proc*   h      = user->get_pending_dl_harq(tti_dl, cell_idx);

    rbgmask_t custom_user_mask;
    bool use_custom_user_mask = false;

    // SCOPE: get custom user mask
    if (user && user->has_slicing_mask && user->slicing_mask.size() > 0) {
        // std::cout << "user mask " << user->slicing_mask.to_string() << std::endl;
        custom_user_mask = user->slicing_mask;
        use_custom_user_mask = true;
    }

    // Schedule retx if we have space
    if (h != nullptr) {
        // Try to reuse the same mask
//        rbgmask_t retx_mask = h->get_rbgmask();

        // Try to reuse the same mask
        rbgmask_t retx_mask;

        // SCOPE: set custom mask
        if (use_custom_user_mask)
            // TODO: check if we can smply set h->get_rbgmask() for retransmissions
            retx_mask = custom_user_mask;
        else
            retx_mask = h->get_rbgmask();

        // SCOPE: debug prints for reTX
        // printf("ReTX: RNTI %" PRIu16 ", size %d, ", user->get_rnti(), (int) retx_mask.size());
        // std::cout << "Custom mask " << use_custom_user_mask << "\n";
        // std::cout << "retx_mask " << retx_mask.to_string() << "\n";
        // std::cout << "h_mask " << h->get_rbgmask().to_string() << "\n\n";

        code                = tti_alloc->alloc_dl_user(user, retx_mask, h->get_id());
        if (code == alloc_outcome_t::SUCCESS) {
            return h;
        }
        if (code == alloc_outcome_t::DCI_COLLISION) {
            // No DCIs available for this user. Move to next
            log_h->warning("SCHED: Couldn't find space in PDCCH for DL retx for rnti=0x%x\n", user->get_rnti());
            return nullptr;
        }

        // If previous mask does not fit, find another with exact same number of rbgs
        size_t nof_rbg = retx_mask.count();
        if (find_allocation(nof_rbg, nof_rbg, &retx_mask)) {
            code = tti_alloc->alloc_dl_user(user, retx_mask, h->get_id());
            if (code == alloc_outcome_t::SUCCESS) {
                return h;
            }
            if (code == alloc_outcome_t::DCI_COLLISION) {
                log_h->warning("SCHED: Couldn't find space in PDCCH for DL retx for rnti=0x%x\n", user->get_rnti());
                return nullptr;
            }
        }
    }

    // If could not schedule the reTx, or there wasn't any pending retx, find an empty PID
    h = user->get_empty_dl_harq(tti_dl, cell_idx);
    if (h != nullptr) {
        // Allocate resources based on pending data
//        rbg_range_t req_rbgs = user->get_required_dl_rbgs(cell_idx);
        rbg_range_t req_rbgs;

        // SCOPE: waterfilling case
        if (fixed_allocation) {
          // convert pre-computed prb into rbg
          req_rbgs = user->get_rbgs_from_prbs(cell_idx, prb_num);
          // printf("Fixed allocation, rnti=0x%x req_rbgs (%d, %d)\n", user->get_rnti(), req_rbgs.rbg_min, req_rbgs.rbg_max);
        }
        else {
          // SCOPE: get number of rbgs from srsLTE default procedures
          req_rbgs = user->get_required_dl_rbgs(cell_idx);
        }

        if (req_rbgs.rbg_min > 0) {

            // SCOPE: set custom mask
            rbgmask_t newtx_mask;
            if (use_custom_user_mask) {
                // SCOPE: update slicing mask after one user
                // has been allocated some of the available RBGs in the slicing mask
                newtx_mask = rbgmask_t(custom_user_mask.size());
                find_allocation_slicing(req_rbgs.rbg_min, req_rbgs.rbg_max, slicing_structure[user->slice_number].tti_slicing_mask, &newtx_mask);
            }
            else {
                newtx_mask = rbgmask_t(tti_alloc->get_dl_mask().size());

                // SCOPE, comment only: find_allocation overwrites the current mask,
                // which in this case is not set by our slicing policies, so it is fine
                find_allocation(req_rbgs.rbg_min, req_rbgs.rbg_max, &newtx_mask);
            }

            // SCOPE: get number of RBG granted
            uint32_t rbg_granted = newtx_mask.count();

            // debug print
            // printf("RNTI %" PRIu16 ", size %d, RGB granted %" PRIu32 ", ", user->get_rnti(), (int) newtx_mask.size(), rbg_granted);
            // std::cout << "newtx_mask " << newtx_mask.to_string() << "\n";
            // for (int i = 0; i < (int) newtx_mask.size(); ++i)
            //     std::cout << "mask[" << i << "] " << newtx_mask.get(i) << "\n";

            // SCOPE: get granted PRBs
            uint32_t prb_granted = get_granted_prbs_from_rbg(req_rbgs.rbg_max, rbg_granted, newtx_mask.get(newtx_mask.size() - 1));

            // SCOPE: save prbs into stucture to periodically dump on csv file
            // printf("RNTI %" PRIu16 ", PRB granted %" PRIu32 "\n", user->get_rnti(), prb_granted);
            ue_resources[get_ue_idx_from_rnti(user->get_rnti())].sum_granted_prbs += prb_granted;

            // SCOPE: check with any() instead of trying an allocation,
            // as find_allocation would overwrite the mask set by our slicing policies
            if (newtx_mask.any()) {
                // some empty spaces were found
                code = tti_alloc->alloc_dl_user(user, newtx_mask, h->get_id());
                if (code == alloc_outcome_t::SUCCESS) {
                    return h;
                } else if (code == alloc_outcome_t::DCI_COLLISION) {
                    log_h->warning("SCHED: Couldn't find space in PDCCH for DL tx for rnti=0x%x\n",
                                   user->get_rnti());
                }
            }
        }
    }

    return nullptr;
}

//dl_harq_proc* dl_metric_rr::allocate_user(sched_ue* user)
//{
//  // Do not allocate a user multiple times in the same tti
//  if (tti_alloc->is_dl_alloc(user)) {
//    return nullptr;
//  }
//  // Do not allocate a user to an inactive carrier
//  auto p = user->get_cell_index(cc_cfg->enb_cc_idx);
//  if (not p.first) {
//    return nullptr;
//  }
//  uint32_t cell_idx = p.second;
//
//  alloc_outcome_t code;
//  uint32_t        tti_dl = tti_alloc->get_tti_tx_dl();
//  dl_harq_proc*   h      = user->get_pending_dl_harq(tti_dl, cell_idx);
//
//  // Schedule retx if we have space
//  if (h != nullptr) {
//    // Try to reuse the same mask
//    rbgmask_t retx_mask = h->get_rbgmask();
//    code                = tti_alloc->alloc_dl_user(user, retx_mask, h->get_id());
//    if (code == alloc_outcome_t::SUCCESS) {
//      return h;
//    }
//    if (code == alloc_outcome_t::DCI_COLLISION) {
//      // No DCIs available for this user. Move to next
//      log_h->warning("SCHED: Couldn't find space in PDCCH for DL retx for rnti=0x%x\n", user->get_rnti());
//      return nullptr;
//    }
//
//    // If previous mask does not fit, find another with exact same number of rbgs
//    size_t nof_rbg = retx_mask.count();
//    if (find_allocation(nof_rbg, nof_rbg, &retx_mask)) {
//      code = tti_alloc->alloc_dl_user(user, retx_mask, h->get_id());
//      if (code == alloc_outcome_t::SUCCESS) {
//        return h;
//      }
//      if (code == alloc_outcome_t::DCI_COLLISION) {
//        log_h->warning("SCHED: Couldn't find space in PDCCH for DL retx for rnti=0x%x\n", user->get_rnti());
//        return nullptr;
//      }
//    }
//  }
//
//  // If could not schedule the reTx, or there wasn't any pending retx, find an empty PID
//  h = user->get_empty_dl_harq(tti_dl, cell_idx);
//  if (h != nullptr) {
//    // Allocate resources based on pending data
//    rbg_range_t req_rbgs = user->get_required_dl_rbgs(cell_idx);
//    if (req_rbgs.rbg_min > 0) {
//      rbgmask_t newtx_mask(tti_alloc->get_dl_mask().size());
//      if (find_allocation(req_rbgs.rbg_min, req_rbgs.rbg_max, &newtx_mask)) {
//        // some empty spaces were found
//        code = tti_alloc->alloc_dl_user(user, newtx_mask, h->get_id());
//        if (code == alloc_outcome_t::SUCCESS) {
//          return h;
//        } else if (code == alloc_outcome_t::DCI_COLLISION) {
//          log_h->warning("SCHED: Couldn't find space in PDCCH for DL tx for rnti=0x%x\n", user->get_rnti());
//        }
//      }
//    }
//  }
//
//  return nullptr;
//}

/*****************************************************************
 *
 * Uplink Metric
 *
 *****************************************************************/

void ul_metric_rr::set_params(const sched_cell_params_t& cell_params_)
{
  cc_cfg = &cell_params_;
  log_h  = srslte::logmap::get("MAC ");
}

void ul_metric_rr::sched_users(std::map<uint16_t, sched_ue>& ue_db, ul_sf_sched_itf* tti_sched)
{
  tti_alloc   = tti_sched;
  current_tti = tti_alloc->get_tti_tx_ul();

  if (ue_db.empty()) {
    return;
  }


  if (network_slicing_enabled) {

  // copy slicing mask to tti slicing mask. Do this at every new tti
    for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {
      for (int rbg_idx = 0; rbg_idx < MAX_MASK_LENGTH; ++rbg_idx) {
        slicing_structure[s_idx].ul_tti_slicing_mask[rbg_idx] = slicing_structure[s_idx].ul_slicing_mask[rbg_idx];
      }
    }
  }

  // give priority in a time-domain RR basis
  uint32_t priority_idx =
      (current_tti + (uint32_t)ue_db.size() / 2) % (uint32_t)ue_db.size(); // make DL and UL interleaved

  // allocate reTxs first
  auto iter = ue_db.begin();
  std::advance(iter, priority_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin(); // wrap around
    }
    sched_ue* user = &iter->second;
    allocate_user_retx_prbs(user);
  }

  // give priority in a time-domain RR basis
  iter = ue_db.begin();
  std::advance(iter, priority_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin(); // wrap around
    }
    sched_ue* user = &iter->second;
    allocate_user_newtx_prbs(user);
  }
}

/**
 * Finds a range of L contiguous PRBs that are empty
 * @param L Size of the requested UL allocation in PRBs
 * @param alloc Found allocation. It is guaranteed that 0 <= alloc->L <= L
 * @return true if the requested allocation of size L was strictly met
 */
bool ul_metric_rr::find_allocation(uint32_t L, ul_harq_proc::ul_alloc_t* alloc)
{
  const prbmask_t* used_rb = &tti_alloc->get_ul_mask();
  bzero(alloc, sizeof(ul_harq_proc::ul_alloc_t));
  for (uint32_t n = 0; n < used_rb->size() && alloc->L < L; n++) {
    if (not used_rb->test(n) && alloc->L == 0) {
      alloc->RB_start = n;
    }
    if (not used_rb->test(n)) {
      alloc->L++;
    } else if (alloc->L > 0) {
      // avoid edges
      if (n < 3) {
        alloc->RB_start = 0;
        alloc->L        = 0;
      } else {
        break;
      }
    }
  }
  if (alloc->L == 0) {
    return false;
  }

  // Make sure L is allowed by SC-FDMA modulation
  while (!srslte_dft_precoding_valid_prb(alloc->L)) {
    alloc->L--;
  }
  return alloc->L == L;
}

bool ul_metric_rr::find_allocation_slicing(uint32_t L, ul_harq_proc::ul_alloc_t* alloc, uint8_t* tti_slicing_mask, uint32_t P)
{
  const prbmask_t* used_rb = &tti_alloc->get_ul_mask();
  bzero(alloc, sizeof(ul_harq_proc::ul_alloc_t));

  // Find allowed PRB according to slice RBGs
  prbmask_t unavailable_rb(used_rb->size());

  // set unavailable rbs according to tti_slicing_mask
  for (int i = 0; i < cell_rbgs; ++i) {
    // if the RBG is unavailable
    if (tti_slicing_mask[i] == 0) {
      // then set the corresponding RBs as unavailable
      for (size_t j = 0; j < P && i*P+j < unavailable_rb.size(); ++j) {
        unavailable_rb.set(P*i+j);
      }
    }
  }

  // the unavailable RBs are both those not included in the slices and those already allocated
  unavailable_rb |= *used_rb;

  for (uint32_t n = 0; n < unavailable_rb.size() && alloc->L < L; n++) {
    if (not unavailable_rb.test(n) && alloc->L == 0) {
      alloc->RB_start = n;
    }
    if (not unavailable_rb.test(n)) {
      alloc->L++;
    } else if (alloc->L > 0) {
      // avoid edges
      if (n < 3) {
        alloc->RB_start = 0;
        alloc->L        = 0;
      } else {
        break;
      }
    }
  }
  if (alloc->L == 0) {
    return false;
  }


  // update tti_slicing_mask according to the assigned RBs
  for (int i = 0; i < cell_rbgs; i++) {
    // if one of the RBs composing the RBG is occupied
    bool any = false;
    for (size_t j = 0; j < P && i*P+j < unavailable_rb.size(); ++j) {
      if (unavailable_rb.test(i*P+j)) {
        any = true;
        break;
      }
    }
    // then the RBG is occupied
    if (any) {
      tti_slicing_mask[i] = 0;
    }
  }

  // Make sure L is allowed by SC-FDMA modulation
  while (!srslte_dft_precoding_valid_prb(alloc->L)) {
    alloc->L--;
  }
  return alloc->L == L;
}

ul_harq_proc* ul_metric_rr::allocate_user_retx_prbs(sched_ue* user)
{
  if (tti_alloc->is_ul_alloc(user)) {
    return nullptr;
  }
  auto p = user->get_cell_index(cc_cfg->enb_cc_idx);
  if (not p.first) {
    // this cc is not activated for this user
    return nullptr;
  }
  uint32_t cell_idx = p.second;

  alloc_outcome_t ret;
  ul_harq_proc*   h = user->get_ul_harq(current_tti, cell_idx);

  // if there are procedures and we have space
  if (h->has_pending_retx()) {
    ul_harq_proc::ul_alloc_t alloc = h->get_alloc();

    // If can schedule the same mask, do it
    ret = tti_alloc->alloc_ul_user(user, alloc);
    if (ret == alloc_outcome_t::SUCCESS) {
      return h;
    }
    if (ret == alloc_outcome_t::DCI_COLLISION) {
      log_h->warning("SCHED: Couldn't find space in PDCCH for UL retx of rnti=0x%x\n", user->get_rnti());
      return nullptr;
    }

    if (find_allocation(alloc.L, &alloc)) {
      ret = tti_alloc->alloc_ul_user(user, alloc);
      if (ret == alloc_outcome_t::SUCCESS) {
        return h;
      }
      if (ret == alloc_outcome_t::DCI_COLLISION) {
        log_h->warning("SCHED: Couldn't find space in PDCCH for UL retx of rnti=0x%x\n", user->get_rnti());
      }
    }
  }
  return nullptr;
}

ul_harq_proc* ul_metric_rr::allocate_user_newtx_prbs(sched_ue* user)
{
  if (tti_alloc->is_ul_alloc(user)) {
    return nullptr;
  }
  auto p = user->get_cell_index(cc_cfg->enb_cc_idx);
  if (not p.first) {
    // this cc is not activated for this user
    return nullptr;
  }
  uint32_t cell_idx = p.second;

  uint32_t      pending_data = user->get_pending_ul_new_data(current_tti);
  ul_harq_proc* h            = user->get_ul_harq(current_tti, cell_idx);


  bool use_custom_mask = false;

  if (network_slicing_enabled && user && user->slice_number > -1) {
    use_custom_mask = true;
  }

  // find an empty PID
  if (h->is_empty(0) and pending_data > 0) {
    uint32_t                 pending_rb = user->get_required_prb_ul(cell_idx, pending_data);
    ul_harq_proc::ul_alloc_t alloc{};

    if (use_custom_mask) {
        find_allocation_slicing(pending_rb, &alloc, slicing_structure[user->slice_number].ul_tti_slicing_mask, cc_cfg->P);
    }
    else {
        find_allocation(pending_rb, &alloc);
    }

    if (alloc.L > 0) { // at least one PRB was scheduled
      alloc_outcome_t ret = tti_alloc->alloc_ul_user(user, alloc);
      if (ret == alloc_outcome_t::SUCCESS) {
        return h;
      }
      if (ret == alloc_outcome_t::DCI_COLLISION) {
        log_h->warning("SCHED: Couldn't find space in PDCCH for UL tx of rnti=0x%x\n", user->get_rnti());
      }
    }
  }
  return nullptr;
}

} // namespace srsenb
