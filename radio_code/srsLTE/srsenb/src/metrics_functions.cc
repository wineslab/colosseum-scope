// Metric functions used by SCOPE

#include "srsenb/hdr/metrics_functions.h"
#include "srsenb/hdr/global_variables.h"

// for flock
#include <sys/file.h>

// for fileno
#include <stdio.h>
#include <cstdlib>
//#include <cstring>
#include <math.h>

// for access and fchmod
#include <unistd.h>
#include <sys/stat.h>

#include <string>

// for timestamp
#include <time.h>
#include <sys/timeb.h>

#include <sstream>
#include <cstring>

#include <inttypes.h>
#include <srslte/phy/common/phy_common.h>
#include <iostream>
#include <fstream>
#include <srsenb/hdr/ue_imsi_functions.h>
#include <srsenb/hdr/ue_rnti_functions.h>

int cell_prbs_global;

users_resources ue_resources[500] = {{0, 0, 0,
                                             0, 0,
                                             1.0, 0,
                                              0,
                                             0, 0,
                                              0, 0, 0}};

// last time metrics were written on csv
// NOTE: only update in save_ue_metrics_to_csv not to break throughput computation
long int last_time_metrics_on_csv_ms;

const double sinr_to_cqi[4][16]= { {-2.5051, -2.5051, -1.7451, -0.3655, 1.0812, 2.4012, 3.6849, 6.6754, 8.3885, 8.7970, 12.0437, 14.4709, 15.7281,  17.2424,  17.2424, 17.2424},
                                   {-2.2360, -2.2360, -1.3919, -0.0218, 1.5319,  2.9574,  4.3234, 6.3387, 8.9879, 9.5096, 12.6609, 14.0116, 16.4984, 18.1572, 18.1572, 18.1572},
                                   {-1, -1.0000, -0.4198, -0.0140, 1.0362,  2.3520, 3.5793, 6.1136, 8.4836, 9.0858, 12.4723, 13.9128, 16.2054, 17.7392, 17.7392, 17.7392},
                                   { -4.1057, -4.1057, -3.3768, -2.2916, -1.1392, 0.1236, 1.2849, 3.1933, 5.9298, 6.4052, 9.6245, 10.9414, 13.5166, 14.9545, 14.9545, 14.9545}
};

// inverting 36.213 Table 7.2.3-1
// set to highest CQI for the target modulation
// 0 = do not force, 1 = QPSK, 2 = 16QAM, 3 = 64QAM
const uint32_t modulation_to_cqi[4] = {0, 5, 8, 15};

// approximate SINR from CQI
double sinr_from_cqi(uint32_t transmission_mode, uint32_t cqi) {
    return sinr_to_cqi[transmission_mode][cqi];
}

// get CQI needed to force target modulation
uint32_t cqi_from_modulation(int modulation) {
    return modulation_to_cqi[modulation];
}

// log user metric - ue_rnti::metric_value::timestamp
// keep a file with the most recent metric for each user and another one
// to log all the user metrics
void save_user_metric_and_log(uint16_t ue_rnti, double metric_value, std::string metric_name, std::string config_dir_path) {

    // file variables
    FILE* metric_file;
    FILE* temp_file;

    // file in which to write single metric and log file
    std::string metric_file_name;
    std::string temp_file_name;

    // new line to write on file
    std::string new_line;

    std::string in_line_delimiter = "::";

    // Unix timestamp
    long int timestamp;

    // convert ue_rnti to string
    std::string rnti_char;
    std::stringstream rnti_ss;
    rnti_ss << ue_rnti;
    rnti_char = rnti_ss.str();

    // variable indicating if the file existed before
    int file_existed_before;

    // variable to check if user is already present on file
    int rnti_already_present = 0;

    // read user rnti
    int read_ue_rnti = -1;

    // Variables to read lines
    ssize_t num_read_elem;
    char* line = NULL;
    size_t len = 0;

    // return variable for flock
    int ret_fl1;
    int ret_fl2;

    // compute timestamp
    timestamp = get_time_milliseconds();

    // assemble new_line
    std::stringstream new_line_ss;
    new_line_ss << ue_rnti << in_line_delimiter << metric_value << in_line_delimiter << timestamp << std::endl;
    new_line = new_line_ss.str();

    // get path of environment variable $HOME and form absolute path
    metric_file_name = config_dir_path;

    // build temp_file_name
    temp_file_name = metric_file_name;
    temp_file_name += "temp.txt";

    // build metric_file_name
    metric_file_name += metric_name + ".txt";

    // open metric file and temporary file
    metric_file = fopen(metric_file_name.c_str(), "r");
    temp_file = fopen(temp_file_name.c_str(), "w");

    // check if file exists
    if (metric_file == NULL) {
        // create empty configuration file
        metric_file = fopen(metric_file_name.c_str(), "w");
        fclose(metric_file);
        metric_file = fopen(metric_file_name.c_str(), "r");

        file_existed_before = 0;
    } else {
        file_existed_before = 1;
    }

    // Lock files
    ret_fl1 = flock(fileno(metric_file), LOCK_EX);
    ret_fl2 = flock(fileno(temp_file), LOCK_EX);

    // check flock return value
    if (ret_fl1 == -1 || ret_fl2 == -1) {
        fclose(metric_file);
        fclose(temp_file);
        remove(temp_file_name.c_str());
        printf("save_user_metric_and_log: flock return value is -1\n");
//        exit(0);
        return;
    }

    while ((num_read_elem = getline(&line, &len, metric_file)) != -1) {
        // copy line to find user id
        char line_copy[num_read_elem + 1];
        strncpy(line_copy, line, num_read_elem);
        line_copy[num_read_elem] = '\0';

        // get user id and value
        char *delimiter_ptr = strtok(line_copy, in_line_delimiter.c_str());
        read_ue_rnti = strtol(line, &delimiter_ptr, 10);

        if (read_ue_rnti == ue_rnti) {
            // write new_line on temp_file
            fputs(new_line.c_str(), temp_file);

            rnti_already_present = 1;
        } else {
            // copy old line on file
            fprintf(temp_file, "%s", line);
        }
    }

    // write new_line on file if file did not existed before or if user rnti was not already present
    if (!file_existed_before || !rnti_already_present) {
        fputs(new_line.c_str(), temp_file);
    }

    // Free memory occupied by line
    if (line)
        free(line);

    // Close files
    fclose(metric_file);
    fclose(temp_file);

    // Unlock
    flock(fileno(metric_file), LOCK_UN);
    flock(fileno(temp_file), LOCK_UN);

    // remove metric_file and rename temp_file
    remove(metric_file_name.c_str());
    rename(temp_file_name.c_str(), metric_file_name.c_str());

    log_user_metric((int) ue_rnti, metric_value, metric_name, config_dir_path, timestamp);
}

// log number of PRB assigned to each user
void log_user_metric(int ue_rnti, float metric_value, std::string metric_name,
                     std::string config_dir_path, long int timestamp) {

    // file variables
    FILE* log_file;

    // file in which to write single metric and log file
    std::string log_file_name;

    // new line to write on file
    std::string new_line;

    std::string in_line_delimiter = "::";

    // convert ue_rnti to string
    std::string rnti_char;
    std::stringstream rnti_ss;
    rnti_ss << ue_rnti;
    rnti_char = rnti_ss.str();

    // return variable for flock
    int ret_fl1;

    // assemble new_line
    std::stringstream new_line_ss;
    new_line_ss << timestamp << in_line_delimiter << metric_value << std::endl;
    new_line = new_line_ss.str();

    // get path of environment variable $HOME and build filename
    log_file_name = config_dir_path + "log/" + rnti_char + "_" + metric_name + ".log";

    // open log file
    log_file = fopen(log_file_name.c_str(), "a");

    // Lock file
    ret_fl1 = flock(fileno(log_file), LOCK_EX);

    // check flock return value
    if (ret_fl1 == -1) {
        fclose(log_file);
        return;
    }

    // append new_line
    fputs(new_line.c_str(), log_file);

    // Close file
    fclose(log_file);

    // Unlock
    flock(fileno(log_file), LOCK_UN);
}

// return current time in milliseconds since the EPOCH
long int get_time_milliseconds(void) {

    struct timeb tmb;
    ftime(&tmb);

    return tmb.time * 1000.0 + ((long int) tmb.millitm);
}

// compute user throughput
void compute_user_throughput(int ue_rnti, int tx_bitrate, float period_s, long int timestamp) {

    float throughput_Mbps;

    // return if RNTI is not that of a user
    if (!is_user(ue_rnti) || ue_rnti > 32000) {
        return;
    }

    throughput_Mbps = ((float) tx_bitrate) / 1048576.0 / period_s;

    // log throughput on file
    std::string metrics_dir_path;
    metrics_dir_path = SCOPE_CONFIG_DIR;
    metrics_dir_path += "metrics/";
    log_user_metric(ue_rnti, throughput_Mbps, "throughput_mbps", metrics_dir_path, timestamp);
}

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
uint32_t get_prbs_per_rbg() {

    uint32_t prbs_per_rbg = 0;

    if (cell_prbs_global == 6)
        prbs_per_rbg = 1;
    else if (cell_prbs_global == 15 || cell_prbs_global == 25)
        prbs_per_rbg = 2;
    else if (cell_prbs_global == 50)
        prbs_per_rbg = 3;
    else
        prbs_per_rbg = 4;

    return prbs_per_rbg;
}

// get number of actually allocated PRBs
uint32_t get_granted_prbs_from_rbg(uint32_t rbg_requested, uint32_t rbg_granted, uint8_t rbg_mask_last_bit) {

    uint32_t prbs_per_rbg;
    uint32_t prb_granted;
    uint32_t prb_requested;

    // get number of PRBs per RBG
    prbs_per_rbg = get_prbs_per_rbg();

    // multiply to get number of allocated PRBs
    prb_granted = rbg_granted * prbs_per_rbg;
    prb_requested = rbg_requested * prbs_per_rbg;

    // adjust number of PRBs if the last mask bit has been allocated (set to 1)
    if (rbg_mask_last_bit == 1) {
        if (cell_prbs_global == 15 || cell_prbs_global == 25 || cell_prbs_global == 50 || cell_prbs_global == 75)
            prb_granted -= 1;
    }

    if (prb_requested < prb_granted)
        prb_granted = prb_requested;

    return prb_granted;
}

// save user metrics in csv file
void save_ue_metrics_to_csv(srsenb::enb_metrics_t* m, float period_s,
                            std::string config_dir_path) {

    long int timestamp = get_time_milliseconds();

    std::string slicing_dir_path;
    slicing_dir_path = SCOPE_CONFIG_DIR;
    slicing_dir_path += "slicing/";

    // get how much time has passed since last time function was called
    float time_passed_s = period_s;
    if (last_time_metrics_on_csv_ms > 0) {
        time_passed_s = ((float) (timestamp - last_time_metrics_on_csv_ms)) / 1000.0;
    }

    // cycle through base station and users and write user metrics on file
    for (int ue_idx = 0; ue_idx < m->stack.rrc.n_ues; ue_idx++) {
        // file in which to write single metric and log file
        std::string csv_file_name;

        long long unsigned int ue_imsi = 0;
        int ue_slice = 0;

        int ue_array_idx = get_ue_idx_from_rnti(m->stack.mac[ue_idx].rnti);

        bool header_needed = false;

        // acquire slice_id if not done yet
        if (network_slicing_enabled && !ue_resources[ue_array_idx].slice_id_acquired &&
            ue_resources[ue_array_idx].imsi_acquired) {

            ue_resources[ue_array_idx].slice_id = (int) get_value_from_imsi(ue_resources[ue_array_idx].imsi,
                                                                            slicing_dir_path, "ue_imsi_slice.txt");

            if (ue_resources[ue_array_idx].slice_id > -1)
                ue_resources[ue_array_idx].slice_id_acquired = 1;
        }
        else if (!network_slicing_enabled) {
            // default to slice 0
            ue_resources[ue_array_idx].slice_id = 0;
            ue_resources[ue_array_idx].slice_id_acquired = 1;
        }

        // default to user rnti if imsi not found
        ue_imsi = ue_resources[ue_array_idx].imsi;
        if (ue_imsi == 0)
            ue_imsi = m->stack.mac[ue_idx].rnti;

        std::string imsi_char;
        std::stringstream imsi_ss;
        imsi_ss << ue_imsi;
        imsi_char = imsi_ss.str();

        csv_file_name = config_dir_path;
        csv_file_name += "csv/";
        csv_file_name += imsi_char;
        csv_file_name += "_metrics.csv";

        // check if file already exists. Need to add file header if it does not exist
        if (access(csv_file_name.c_str(), F_OK) == -1) {
            header_needed = true;
        }

        std::ofstream csv_file;
        csv_file.open(csv_file_name, std::ofstream::out | std::ofstream::app);

        // write header for user metrics
        if (header_needed) {
            // info
            csv_file << "Timestamp" << ','
                     << "num_ues" << ','
                     << "IMSI" << ','
                     << "RNTI" << ','
                     << ','

                     // actions
                     << "slicing_enabled" << ','
                     << "slice_id" << ','
                     << "slice_prb" << ','
                     << "power_multiplier" << ','
                     << "scheduling_policy" << ','
                     << ','

                     // downlink metrics
                     << "dl_mcs" << ','
                     << "dl_n_samples" << ','
                     << "dl_buffer [bytes]" << ','
                     << "tx_brate downlink [Mbps]" << ','
                     << "tx_pkts downlink" << ','
                     << "tx_errors downlink (%)" << ','
                     << "dl_cqi" << ','
                     << ','

                     // uplink metrics
                     << "ul_mcs" << ','
                     << "ul_n_samples" << ','
                     << "ul_buffer [bytes]" << ','
                     << "rx_brate uplink [Mbps]" << ','
                     << "rx_pkts uplink" << ','
                     << "rx_errors uplink (%)" << ','
                     << "ul_rssi" << ','
                     << "ul_sinr" << ','
                     << "phr" << ','
                     << ','

                     // PRBs
                     << "sum_requested_prbs" << ','
                     << "sum_granted_prbs" << ','
                     << ','

                     // additional metrics
                     << "dl_pmi" << ','
                     << "dl_ri" << ','
                     << "ul_n" << ','
                     << "ul_turbo_iters" << '\n';
        }

        /// info
        csv_file << timestamp << ','
                 << m->stack.rrc.n_ues << ','
                 << ue_imsi << ','
                 << m->stack.mac[ue_idx].rnti << ','
                 << ',';

        /// actions
        csv_file << network_slicing_enabled << ',';

        // get user slice and read info from global structure
        if (ue_resources[ue_array_idx].slice_id > -1)
            ue_slice = ue_resources[ue_array_idx].slice_id;   // remains to default 0 otherwise

        csv_file << ue_slice << ','
                 << slicing_structure[ue_slice].slice_prbs << ','
                 << ue_resources[ue_array_idx].power_multiplier << ',';

        if (network_slicing_enabled)
            csv_file << slicing_structure[ue_slice].scheduling_policy << ',';
        else
            csv_file << global_scheduling_policy << ',';

        csv_file << ',';

        /// downlink metrics
        if (std::isnan(m->phy[ue_idx].dl.mcs))
            csv_file << 0 << ',';
        else
            csv_file << SRSLTE_MAX(0.1, m->phy[ue_idx].dl.mcs) << ',';

        csv_file << m->phy[ue_idx].dl.n_samples << ','
                 << m->stack.mac[ue_idx].dl_buffer << ',';

        // downlink throughput
        if (m->stack.mac[ue_idx].tx_brate > 0)
            csv_file << ((float) m->stack.mac[ue_idx].tx_brate) / 1e6 / time_passed_s << ',';
        else
            csv_file << 0 << ',';

        csv_file << m->stack.mac[ue_idx].tx_pkts << ',';

        // downlink errors
        if (m->stack.mac[ue_idx].tx_pkts > 0 && m->stack.mac[ue_idx].tx_errors)
            csv_file << SRSLTE_MAX(0.1, 100.0 * ((float) m->stack.mac[ue_idx].tx_errors) /
                                      ((float) m->stack.mac[ue_idx].tx_pkts)) << ',';
        else
            csv_file << 0 << ',';

        csv_file << SRSLTE_MAX(0.1, m->stack.mac[ue_idx].dl_cqi) << ','
                 << ',';

        /// uplink metrics
        if (std::isnan(m->phy[ue_idx].ul.mcs))
            csv_file << 0 << ",";
        else
            csv_file << SRSLTE_MAX(0.1, m->phy[ue_idx].ul.mcs) << ',';

        csv_file << m->phy[ue_idx].ul.n_samples << ','
                 << m->stack.mac[ue_idx].ul_buffer << ',';

        // uplink throughput
        if (m->stack.mac[ue_idx].rx_brate > 0)
            csv_file << SRSLTE_MAX(0.1, ((float) m->stack.mac[ue_idx].rx_brate) / 1e6 / time_passed_s) << ',';
        else
            csv_file << 0 << ",";

        csv_file << m->stack.mac[ue_idx].rx_pkts << ',';

        // uplink errors
        if (m->stack.mac[ue_idx].rx_pkts > 0 && m->stack.mac[ue_idx].rx_errors > 0)
            csv_file << SRSLTE_MAX(0.1, 100.0 * ((float) m->stack.mac[ue_idx].rx_errors) /
                                      ((float) m->stack.mac[ue_idx].rx_pkts)) << ",";
        else
            csv_file << 0 << ",";

        if (std::isnan(m->phy[ue_idx].ul.rssi))
            csv_file << 0 << ",";
        else
            csv_file << m->phy[ue_idx].ul.rssi << ',';

        if (std::isnan(m->phy[ue_idx].ul.sinr))
            csv_file << 0 << ",";
        else
            csv_file << SRSLTE_MAX(0.1, m->phy[ue_idx].ul.sinr) << ",";

        csv_file << m->stack.mac[ue_idx].phr << ','
                 << ',';

        /// PRBs
        // save sum of user prbs in this time window
        // NOTE: assigned are the requested ones from the user,
        //       granted are the ones actually allocated by the base station
        csv_file << ue_resources[ue_array_idx].sum_assigned_prbs << ','
                 << ue_resources[ue_array_idx].sum_granted_prbs << ','
                 << ',';

        /// additional metrics
        csv_file << m->stack.mac[ue_idx].dl_pmi << ','
                 << m->stack.mac[ue_idx].dl_ri << ',';

        if (std::isnan(m->phy[ue_idx].ul.n))
            csv_file << 0 << ",";
        else
            csv_file << m->phy[ue_idx].ul.n << ',';

        if (std::isnan(m->phy[ue_idx].ul.turbo_iters))
            csv_file << 0;
        else
            csv_file << m->phy[ue_idx].ul.turbo_iters;

        csv_file << '\n';

        csv_file.close();

        // reset prb counters
        ue_resources[ue_array_idx].sum_assigned_prbs = 0;
        ue_resources[ue_array_idx].sum_granted_prbs = 0;
    }

    // update time at which metrics were written on csv
    last_time_metrics_on_csv_ms = get_time_milliseconds();

}
