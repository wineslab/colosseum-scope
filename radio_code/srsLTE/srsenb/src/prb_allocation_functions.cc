// Function used by SCOPE to allocate PRBs to users

#include <inttypes.h>
#include <iostream>
#include <map>

#include "srsenb/hdr/stack/mac/scheduler.h"
#include "../../lib/src/phy/phch/tbs_tables.h"
#include "srsenb/hdr/prb_allocation_functions.h"

#include <srsenb/hdr/global_variables.h>
#include <srsenb/hdr/ue_rnti_functions.h>
#include <srsenb/hdr/slicing_functions.h>

using namespace srsenb;


// build map estimating number of PRBs required by each user
std::map<uint16_t, uint32_t> build_user_prb_map(std::map<uint16_t,sched_ue> &ue_db, uint32_t tti, uint32_t prb_max) {

    uint32_t tbs;
    int req_tbs_idx;

    uint32_t nof_prb;

    // uint32_t prb_min = 4;
    // set minimum PRBs based on total PRB number of the BS
    uint32_t prb_min;
    if (prb_max <= 10) {
        prb_min = 1;
    }
    else if (prb_max <= 26) {
        prb_min = 2;
    }
    else if (prb_max <= 63) {
        prb_min = 3;
    }
    else {
        prb_min = 4;
    }

    // build output map rnti - required PRBs
    std::map<uint16_t, uint32_t> rnti_prb_req;

    // cycle through users
    for(std::map<uint16_t, sched_ue>::iterator iter=ue_db.begin(); iter!=ue_db.end(); ++iter) {
        sched_ue *user = (sched_ue * ) & iter->second;
        uint16_t rnti = (uint16_t) iter->second.get_rnti();

        // set required PRBs to 0 if scheduling policy of the user slice is default round-robin as
        // this map is only used to compute allocations for the other schedulers (e.g., waterfilling and proportional)
        // NOTE: the case in which slicing is disabled is already handled at the calling function
        int ue_array_idx = get_ue_idx_from_rnti(rnti);
        int ue_slice = ue_resources[ue_array_idx].slice_id;
        Slice_Tenants* slicing_struct = get_slicing_structure(ue_slice);
        if (network_slicing_enabled && slicing_struct && slicing_struct->scheduling_policy == 0) {
            rnti_prb_req.insert(std::pair<uint16_t, uint32_t>(rnti, 0));
            continue;
        }

        // get bytes requested by user
        // was user->get_pending_dl_new_data(tti); previously
        uint32_t req_bytes = user->get_pending_dl_new_data();

        // get tbs
        if (req_bytes > 0) {
            for (nof_prb = prb_min; nof_prb < prb_max; ++nof_prb) {
                req_tbs_idx = srslte_ra_tbs_to_table_idx(req_bytes * 8, nof_prb);

                if (req_tbs_idx > 26)
                    req_tbs_idx = 26;

                // get required MCS basing on srslte_ra_dl_mcs_from_tbs_idx function
                int req_mcs = 0;
                for (int i = 0; i < 29; i++) {
                    if (req_tbs_idx == dl_mcs_tbs_idx_table[i]) {
                        req_mcs = i;
                    }
                }

                tbs = get_tbs_dl(req_mcs, nof_prb);
                if (tbs >= req_bytes)
                    break;
            }
        }
        else {
            nof_prb = 0;
        }

        // save prbs into map
        rnti_prb_req.insert(std::pair<uint16_t, uint32_t>(rnti, nof_prb));
    }

    return rnti_prb_req;
}

// get modulation index to use in TBS table
uint32_t get_i_tbs(int mcs_idx) {

    if (mcs_idx < 10)
        return mcs_idx;
    else if (mcs_idx == 10)
        return 9;
    else if (mcs_idx < 17)
        return mcs_idx - 1;
    else if (mcs_idx == 17)
        return 15;
    else return mcs_idx - 2;

}

// get downlink TBS
uint32_t get_tbs_dl(int mcs, uint32_t nb_rb) {

    uint32_t tbs;

    if ((nb_rb > 0) && (mcs < 29)) {
        tbs = tbs_table[get_i_tbs(mcs)][nb_rb - 1];
        tbs = tbs >> 3;
        return tbs;
    } else {
        return 0;
    }
}

// compute waterfilling-like allocation
std::map<uint16_t, uint32_t> compute_waterfilling_allocation(std::map<uint16_t, uint32_t > rnti_prb_req, uint32_t prb_max) {

    // min prb to allocate at the first allocation round
    uint32_t prb_min;

    // min prb to allocate at each allocation round after the first
    uint32_t prb_round;

    // leftover prbs from the RBG allocation
    uint32_t prb_leftover;

    if (prb_max <= 10) {
        prb_min = 1;
    }
    else if (prb_max <= 26) {
        prb_min = 2;
    }
    else if (prb_max <= 63) {
        prb_min = 3;
    }
    else {
        prb_min = 4;
    }

    prb_round = prb_min;

    prb_leftover = prb_round - 1;
    if (prb_leftover == 0) {
      prb_leftover = 1;
    }

    bool any_user = false;

    // trigger print of computed PRB allocation
    bool print_allocations = false;

    // copy input map
    std::map<uint16_t, uint32_t > rnti_prb_req_cpy(rnti_prb_req);

    // initialize output map
    std::map<uint16_t, uint32_t > rnti_prb_alloc;

    // cycle over slices and waterfill in a slice-wise manner
    for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {

      Slice_Tenants* slicing_struct = &slicing_structure[s_idx];

      // skip if slice not active, only run once if network slicing is disabled.
      // Also skip if scheduling of the slice is not waterfilling
      // TODO: also skip if no slicing and global sched is not wf?
      if (network_slicing_enabled && slicing_struct && (slicing_struct->slice_prbs == 0 || slicing_struct->scheduling_policy != 1)) {
        continue;
      }
      else if (!slicing_struct) {
        continue;
      }
      else if (!network_slicing_enabled && s_idx > 0) {
        break;
      }

      // initialize tti PRBs to total PRBs in the slice
      uint32_t tti_slice_prbs;
      if (network_slicing_enabled)
        tti_slice_prbs = slicing_struct->slice_prbs;
      else {
        // use this variable even if slicing is disabled
        tti_slice_prbs = prb_max;
      }

      // initialize support variables
      bool sold_out = false;
      bool ue_needing_prbs = true;

      // number of PRBs allocated to users of the current slice
      uint32_t nof_allocated = 0;

      // set a random position of the iterator to improve fairness
      uint32_t advance_index = rand() % (uint32_t)rnti_prb_req_cpy.size();

      // repeat until sold out
      while (!sold_out && ue_needing_prbs) {

        ue_needing_prbs = false;

        // advance iterator to the precomputed random position to improve fairness
        std::map<uint16_t, uint32_t>::iterator iter = rnti_prb_req_cpy.begin();
        std::advance(iter, advance_index);

        // cycle through users
        // start at pre-computed random iterator position to improve fairness
        for (uint32_t ue_count = 0; ue_count < rnti_prb_req_cpy.size(); ++iter, ++ue_count) {
            // wrap around if at the end
            if (iter == rnti_prb_req_cpy.end()) {
              iter = rnti_prb_req_cpy.begin();
            }

          uint16_t rnti    = (uint16_t)iter->first;
          uint32_t prb_req = (uint32_t)iter->second;

          // skip user if it does not belong to the current slice of the for loop above
          int ue_array_idx = get_ue_idx_from_rnti(rnti);
          if (ue_resources[ue_array_idx].slice_id != slicing_struct->slice_id) {
            continue;
          }

          // check if user needs PRBs
          if (prb_req > 0) {

            ue_needing_prbs = true;

            if (print_allocations)
              any_user = true;

            // insert user in map at the beginning.
            // This is particularly important in case users reconnect after RRC IDLE,
            // as it typically happens with TGEN on Colosseum
            if (rnti_prb_alloc.find(rnti) == rnti_prb_alloc.end()) {
              rnti_prb_alloc.insert(std::pair<uint16_t, uint32_t>(rnti, 0));
            }

            // assign min number of PRBs in the first round
            if ((rnti_prb_alloc.at(rnti) == 0) && (nof_allocated + prb_min <= tti_slice_prbs)) {

              // insert value in map
              rnti_prb_alloc[rnti] = prb_min;

              // update counters
              if (prb_req - prb_min > 0) {
                  rnti_prb_req_cpy[rnti] = prb_req - prb_min;
              }
              else {
                  rnti_prb_req_cpy[rnti] = 0;
              }

              nof_allocated += prb_min;

            } else if (nof_allocated + prb_round <= tti_slice_prbs) {  // allocate one at a time

              // get current value from map
              uint32_t curr_prb = rnti_prb_alloc.at(rnti);

              // insert value in map
              rnti_prb_alloc[rnti] = curr_prb + prb_round;

              // update counters
              if (prb_req - prb_round > 0) {
                  rnti_prb_req_cpy[rnti] = prb_req - prb_round;
              }
              else {
                  rnti_prb_req_cpy[rnti] = 0;
              }

              nof_allocated += prb_round;
            } else if (nof_allocated + prb_leftover <= tti_slice_prbs) {  // allocate one at a time

              // get current value from map
              uint32_t curr_prb = rnti_prb_alloc.at(rnti);

              // insert value in map
              rnti_prb_alloc[rnti] = curr_prb + prb_leftover;

              // update counters
              if (prb_req - prb_leftover > 0) {
                  rnti_prb_req_cpy[rnti] = prb_req - prb_leftover;
              }
              else {
                  rnti_prb_req_cpy[rnti] = 0;
              }

              nof_allocated += prb_leftover;
            }
          }
        }

        // check for sold out
        if (nof_allocated >= tti_slice_prbs || (tti_slice_prbs - nof_allocated) < prb_leftover)
          sold_out = true;
      }
    }

    // print allocations
    if (print_allocations) {
        for (std::map<uint16_t, uint32_t>::iterator iter = rnti_prb_alloc.begin(); iter != rnti_prb_alloc.end(); ++iter) {
            uint16_t rnti = (uint16_t) iter->first;
            uint32_t prb_alloc = (uint32_t) iter->second;

            // only print if requesting some PRBs
            if (rnti_prb_req.at(rnti) > 0)
                printf("RNTI %" PRIu16 ", required %" PRIu32 " PRBs, to allocate %" PRIu32 " PRBs\n", rnti, rnti_prb_req.at(rnti), prb_alloc);
        }

        if (any_user)
            printf("\n");
    }

    return rnti_prb_alloc;
}

// compute proportional allocation
std::map<uint16_t, uint32_t> compute_proportional_allocation(std::map<uint16_t, uint32_t > rnti_prb_req, uint32_t prb_max) {

    // trigger print of computed PRB allocation
    bool print_allocations = false;

    // used for debug prints
    bool any_user = false;

    // build proportional allocation map rnti - proportional PRBs
    std::map<uint16_t, uint32_t> rnti_prb_alloc;

    // cycle over slices
    for (int s_idx = 0; s_idx < MAX_SLICING_TENANTS; ++s_idx) {
        Slice_Tenants* slicing_struct = &slicing_structure[s_idx];

        // skip if slice not active, only run once if network slicing is disabled.
        // Also skip if scheduling of the slice is not waterfilling
        // TODO: also skip if no slicing and global sched is not proportional?
        if (network_slicing_enabled && slicing_struct && (slicing_struct->slice_prbs == 0 || slicing_struct->scheduling_policy != 2)) {
            continue;
        }
        else if (!slicing_struct) {
          continue;
        }
        else if (!network_slicing_enabled && s_idx > 0) {
            break;
        }

        // initialize tti PRBs to total PRBs in the slice
        uint32_t tti_slice_prbs;
        if (network_slicing_enabled)
            tti_slice_prbs = slicing_struct->slice_prbs;
        else {
            // use this variable even if slicing is disabled
            tti_slice_prbs = prb_max;
        }

        double softmax_denominator = 0.0;

        // compute denominator of softmax function
        for (std::map<uint16_t, uint32_t>::iterator den_iter = rnti_prb_req.begin(); den_iter != rnti_prb_req.end(); ++den_iter) {
            uint16_t den_rnti = (uint16_t) den_iter->first;
            uint32_t den_prb_req = (uint32_t) den_iter->second;

            // skip user if it does not belong to the current slice of the for loop above
            int den_ue_array_idx = get_ue_idx_from_rnti(den_rnti);
            if (ue_resources[den_ue_array_idx].slice_id != slicing_struct->slice_id)
                continue;

            // only consider users needing PRBs
            if (den_prb_req > 0) {
                softmax_denominator += exp(den_prb_req);
            }
        }

        uint32_t nof_allocated = 0;

        // compute actual user allocation using softmax function
        for (std::map<uint16_t, uint32_t>::iterator user_iter = rnti_prb_req.begin(); user_iter != rnti_prb_req.end(); ++user_iter) {
            uint16_t user_rnti = (uint16_t) user_iter->first;
            uint32_t user_prb_req = (uint32_t) user_iter->second;

            // skip user if it does not belong to the current slice of the for loop above
            int user_array_idx = get_ue_idx_from_rnti(user_rnti);
            if (ue_resources[user_array_idx].slice_id != slicing_struct->slice_id)
                continue;

            // only consider users needing PRBs
            if (user_prb_req > 0) {
                double softmax_user = exp(user_prb_req) / softmax_denominator;
                softmax_user *= tti_slice_prbs;
                softmax_user = floor(softmax_user);

                // save allocation
                uint32_t allocated_user = std::min((uint32_t) softmax_user, user_prb_req);
                rnti_prb_alloc.insert(std::pair<uint16_t, uint32_t>(user_rnti, allocated_user));
                nof_allocated += allocated_user;
            }
        }

        // randomly assign extra PRBs if users received less than what they need
        if (nof_allocated > 0) {
            any_user = true;

            // set a random position of the iterator to improve fairness
            uint32_t advance_index = rand() % (uint32_t)rnti_prb_alloc.size();
            std::map<uint16_t, uint32_t>::iterator lft_iter = rnti_prb_alloc.begin();
            std::advance(lft_iter, advance_index);

            bool need_prbs = true;

            while (need_prbs && nof_allocated < tti_slice_prbs) {
                // get user with smaller ratio of allocated / requested PRBs
                double min_prb_ratio = 1.1;
                uint16_t min_prb_rnti = 0;
                for (std::map<uint16_t, uint32_t>::iterator prb_iter = rnti_prb_alloc.begin(); prb_iter != rnti_prb_alloc.end(); ++prb_iter) {
                    uint16_t curr_rnti = (uint16_t)prb_iter->first;

                    // skip user if it does not belong to the current slice of the for loop above
                    int curr_ue_array_idx = get_ue_idx_from_rnti(curr_rnti);
                    if (ue_resources[curr_ue_array_idx].slice_id != slicing_struct->slice_id)
                        continue;

                    double prb_ratio = ((double) rnti_prb_alloc.at(curr_rnti)) / ((double) rnti_prb_req.at(curr_rnti));
                    if (prb_ratio < 1.0 && prb_ratio < min_prb_ratio) {
                        min_prb_ratio = prb_ratio;
                        min_prb_rnti = curr_rnti;
                    }
                }

                // assign extra PRB to user with min ratio of allocated / requested PRBs
                if (min_prb_rnti > 0) {
                    rnti_prb_alloc[min_prb_rnti] += 1;
                    nof_allocated += 1;
                }
                else {
                    // either we finished the available PRBs or
                    // all the users have been allocated what they asked for
                    need_prbs = false;
                }
            }
        }
    }

    if (print_allocations) {
        for (std::map<uint16_t, uint32_t>::iterator iter = rnti_prb_alloc.begin(); iter != rnti_prb_alloc.end(); ++iter) {
            uint16_t rnti = (uint16_t) iter->first;
            uint32_t prb_alloc = (uint32_t) iter->second;

            // only print if requesting some PRBs
            if (rnti_prb_req.at(rnti) > 0)
                printf("RNTI %" PRIu16 ", required %" PRIu32 " PRBs, to allocate %" PRIu32 " PRBs\n", rnti, rnti_prb_req.at(rnti), prb_alloc);
        }

        if (any_user > 0)
            printf("\n");
    }

    return rnti_prb_alloc;
}
