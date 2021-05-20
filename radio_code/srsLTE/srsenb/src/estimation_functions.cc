// Estimation functions used by SCOPE

#include <math.h>
#include <stdint-gcc.h>
#include <stdlib.h>
#include <string>
#include <cstdio>
#include <inttypes.h>

#include <iostream>
#include <srsenb/hdr/estimation_functions.h>
#include <srsenb/hdr/global_variables.h>
#include <srsenb/hdr/metrics_functions.h>
#include <srsenb/hdr/ue_rnti_functions.h>
#include <sstream>

const int frequency_ch_coeff_write_ms = 60000;

// compute user downlink channel coefficient and save it in eNB->UE_stats[UE_id].dl_channel_coeff
double compute_ue_dl_channel_coefficient(uint16_t rnti, double tx_data_power_avg) {

    long int timestamp_ms = get_time_milliseconds();

    // get user structure
    int ue_array_idx = get_ue_idx_from_rnti(rnti);
    users_resources* current_ue = &ue_resources[ue_array_idx];

    // get user slice
    int ue_slice = current_ue->slice_id;

    // noise power
    double N0_dB = 0.0055;
    double N0_lin;

    // channel coefficient estimate - absolute value squared
    double g;

    double dl_sinr_lin;
    double eNB_tx_power_lin;

    double dl_sinr_db;
    double power_multiplier;

    // get downlink sinr and power multiplier from user structure
    dl_sinr_db = current_ue->dl_sinr;
    power_multiplier = current_ue->power_multiplier;

    // compute and save channel coefficient (absolute value squared)
    if (dl_sinr_db <= 10000 && power_multiplier <= 10000) {

        // compute linear values from dB quantities
        dl_sinr_lin = pow(10.0, dl_sinr_db / 10.0);
        N0_lin = pow(10.0, N0_dB / 10.0);

        // account for squared value of power multiplier because the multiplier is used in a power multiplication and, thus,
        // its squared value is to be used
        eNB_tx_power_lin = pow(10.0, tx_data_power_avg / 10.0) / pow(power_multiplier, 2.0);

        g = dl_sinr_lin * N0_lin / eNB_tx_power_lin;

        // it can be equal to 1 at the very beginning, when the dB value
        //  has not been computed yet and, thus, is 0 by default
        if (eNB_tx_power_lin > 1.0 && g > 0.0) {
            // save it on file once in a while.
            // NOTE: timestamp_power_multiplier_read is updated in cc_worker.cc
            if (timestamp_ms - current_ue->timestamp_power_multiplier_read > frequency_ch_coeff_write_ms) {
                std::string metrics_dir_path;
                metrics_dir_path = SCOPE_CONFIG_DIR;
                metrics_dir_path += "metrics/";
                save_user_metric_and_log(rnti, g, "dl_channel_coeff", metrics_dir_path);
            }

            return g;
        }
    }

    return -1;
}
