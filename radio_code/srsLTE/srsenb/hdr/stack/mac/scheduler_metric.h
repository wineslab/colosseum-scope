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

#ifndef SRSENB_SCHEDULER_METRIC_H
#define SRSENB_SCHEDULER_METRIC_H

#include "scheduler.h"

namespace srsenb {

class dl_metric_rr : public sched::metric_dl
{
  const static int MAX_RBG = 25;

public:
  // SCOPE: add constructor to initialize variables
  dl_metric_rr();

  void set_params(const sched_cell_params_t& cell_params_) final;
  void sched_users(std::map<uint16_t, sched_ue>& ue_db, dl_sf_sched_itf* tti_sched) final;

  // SCOPE: line to read for slicing allocation map
  int slicing_line_no;

  // SCOPE: last time slice allocation mask line was changed [ms]
  long int last_time_line_changed_ms;

private:
  bool          find_allocation(uint32_t min_nof_rbg, uint32_t max_nof_rbg, rbgmask_t* rbgmask);

  // SCOPE: find allocation in case of slicing
  bool find_allocation_slicing(uint32_t min_nof_rbg, uint32_t max_nof_rbg, uint8_t* tti_slicing_mask, rbgmask_t* rbgmask);

//  dl_harq_proc* allocate_user(sched_ue* user);

  // SCOPE: pass few extra parameters for fixed allocation in case of precomputed PRBs
  // (e.g., in case of waterfilling scheduling allocation)
  dl_harq_proc* allocate_user(sched_ue* user, uint32_t prb_num, bool fixed_allocation);

  const sched_cell_params_t* cc_cfg = nullptr;
  srslte::log_ref            log_h;
  dl_sf_sched_itf*           tti_alloc = nullptr;
};

class ul_metric_rr : public sched::metric_ul
{
public:
  void set_params(const sched_cell_params_t& cell_params_) final;
  void sched_users(std::map<uint16_t, sched_ue>& ue_db, ul_sf_sched_itf* tti_sched) final;

private:
  bool          find_allocation(uint32_t L, ul_harq_proc::ul_alloc_t* alloc);

  //SCOPE: find allocation in case of slicing
  bool          find_allocation_slicing(uint32_t L, ul_harq_proc::ul_alloc_t* alloc, uint8_t* tti_slicing_mask, uint32_t P);

  ul_harq_proc* allocate_user_newtx_prbs(sched_ue* user);
  ul_harq_proc* allocate_user_retx_prbs(sched_ue* user);

  const sched_cell_params_t* cc_cfg = nullptr;
  srslte::log_ref            log_h;
  ul_sf_sched_itf*           tti_alloc   = nullptr;
  uint32_t                   current_tti = 0;
};

} // namespace srsenb

#endif // SRSENB_SCHEDULER_METRIC_H
