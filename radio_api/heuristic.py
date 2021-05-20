import argparse
import ast
import collections
import json
import logging
import os
import time

from scope_start import read_metrics, get_metric_value, get_slice_users,\
    read_slice_scheduling, read_slice_mask, write_tenant_slicing_mask,\
    write_slice_scheduling


# average specified user metric
def average_metric(metrics_dict: collections.OrderedDict(), metric_name: 'str') -> dict:

    out_dict = dict()

    # extract passed metric
    metrics = get_metric_value(metrics_db, metric_name)

    for imsi_key, metrics_val in metrics.items():
        tmp_list = list()

        for _, val in metrics_val.items():
            tmp_list.append(float(val[metric_name]))

        mean = sum(tmp_list) / len(tmp_list)

        out_dict[imsi_key] = mean

    return out_dict


# average metrics over slice
# this function modifies the passed slice_metrics
def avg_slice_metrics(slice_metrics: dict, slice_users: dict, metric_dict: dict, metric_name: str) -> None:

    for s_key, s_val in slice_users.items():
        tmp_metrics = list()
        for imsi in s_val:
            tmp_metrics.append(metric_dict[imsi])

        mean = sum(tmp_metrics) / len(tmp_metrics)

        # insert in passed dictionary
        slice_metrics[s_key][metric_name] = mean


# implement heuristic policy
# in this case, assign more resources to slices if dl_buffer is above threshold (slice 0),
# and dl_thr is above threshold (slice 2)
# if there are more users also change scheduling policy from round-robin to waterfilling
def implement_heuristic(heuristic_config: dict, slice_metrics: dict, usr_kw: str, buff_kw: str, thr_kw: str) -> None:

    # get timestamp for logging purposes
    timestamp_ms = int(time.time() * 1000)

    urllc_slice = 0
    broadband_slice = 1

    urllc_free_rbg_idx = 2
    broadband_free_rbg_idx = 5

    try:
        # get buffer thresholds
        buffer_config_kw = 'buffer_thresh_bytes'
        buffer_thresh_bytes_lower = heuristic_config[buffer_config_kw][0]
        buffer_thresh_bytes_upper = heuristic_config[buffer_config_kw][1]
    except KeyError:
        buffer_thresh_bytes_lower = 1000
        buffer_thresh_bytes_upper = 2000

        logging.warning('Key ' + buffer_config_kw + ' not found in heuristic_config. Using default parameters')

    try:
        # get throughput thresholds
        thr_config_kw = 'thr_thresh_mbps'
        thr_thresh_mbps_lower = heuristic_config[thr_config_kw][0]
        thr_thresh_mbps_upper = heuristic_config[thr_config_kw][1]
    except KeyError:
        thr_thresh_mbps_lower = 0.25
        thr_thresh_mbps_upper = 0.75

        logging.warning('Key ' + thr_config_kw + ' not found in heuristic_config. Using default parameters')

    round_robin_policy = 0
    waterfilling_policy = 1

    # read current slice scheduling allocation
    # index in list corresponds to slice index
    curr_sched = read_slice_scheduling()
    slicing_scheduling_to_write = False

    for s_key, s_val in slice_metrics.items():
        mask_to_write = False

        if s_key == urllc_slice or s_key == broadband_slice:
            # get right parameters
            if s_key == urllc_slice:
                curr_kw = buff_kw
                curr_thresh_upper = buffer_thresh_bytes_upper
                curr_thresh_lower = buffer_thresh_bytes_lower
                curr_free_rbg = urllc_free_rbg_idx
            else:
                curr_kw = thr_kw
                curr_thresh_upper = thr_thresh_mbps_upper
                curr_thresh_lower = thr_thresh_mbps_lower
                curr_free_rbg = broadband_free_rbg_idx

            if s_val[curr_kw] > curr_thresh_upper:
                # get current slicing mask
                curr_slice_mask = list(read_slice_mask(s_key))

                # assign more resources
                if curr_slice_mask[curr_free_rbg] != '1':
                    curr_slice_mask[curr_free_rbg] = '1'
                    curr_slice_mask = ''.join(curr_slice_mask)

                    mask_to_write = True

                # change scheduling to waterfilling if it is not already and
                # if there is more than one user in the current slice
                if s_val[usr_kw] > 1 and curr_sched[s_key] != waterfilling_policy:
                    curr_sched[s_key] = waterfilling_policy
                    slicing_scheduling_to_write = True

            elif s_val[curr_kw] < curr_thresh_lower:
                # get current slicing mask
                curr_slice_mask = list(read_slice_mask(s_key))

                # assign more resources
                if curr_slice_mask[curr_free_rbg] != '0':
                    curr_slice_mask[curr_free_rbg] = '0'
                    curr_slice_mask = ''.join(curr_slice_mask)

                    mask_to_write = True

                # change scheduling to round-robin if it is not already and
                # if there is more than one user in the current slice
                if s_val[usr_kw] > 1 and curr_sched[s_key] != round_robin_policy:
                    curr_sched[s_key] = round_robin_policy
                    slicing_scheduling_to_write = True

            # write mask on file
            if mask_to_write:
                # assemble config parameters dictionary and write mask
                # tenant_number needs to be there but is not used in this case
                config_params = {'network_slicing_enabled': True, 'tenant_number': 1, 'slice_allocation': curr_slice_mask}
                write_tenant_slicing_mask(config_params, True, s_key)
                logging.info('at timestamp_ms ' + str(timestamp_ms) + '\n')

    if slicing_scheduling_to_write:
        # assemble config parameters dictionary and write scheduling
        config_params = {'network_slicing_enabled': True, 'slice_scheduling_policy': curr_sched}
        write_slice_scheduling(config_params)
        logging.info('at timestamp_ms ' + str(timestamp_ms) + '\n')


# get heuristic paraemters from configuration file
def parse_heuristic_config_file(filename: str, param_key: str) -> dict:

    logging.info('Parsing ' + filename + ' configuration file')

    heuristic_params = dict()

    with open(filename, 'r') as file:
        config = json.load(file)

    try:
        heuristic_params = ast.literal_eval(config[param_key])
    except KeyError:
        logging.warning('Key ' + param_key + ' not in ' + filename)

    return heuristic_params


if __name__ == '__main__':

    # Define command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('--config-file', type=str, required=True, help='Configuration file to parse.')
    args = parser.parse_args()

    # configure logger and console output
    logging.basicConfig(level=logging.DEBUG, filename='/logs/heuristic.log', filemode='a+',
        format='%(asctime)-15s %(levelname)-8s %(message)s')
    formatter = logging.Formatter('%(asctime)-15s %(levelname)-8s %(message)s')
    console = logging.StreamHandler()
    console.setLevel(logging.INFO)
    console.setFormatter(formatter)
    logging.getLogger('').addHandler(console)

    seconds_to_read = 10
    lines_to_read = 4 * seconds_to_read

    users_num_keyword = 'slice_users'
    dl_buffer_keyword = 'dl_buffer [bytes]'
    dl_thr_keyword = 'tx_brate downlink [Mbps]'

    filename = os.path.expanduser('~/radio_api/')
    filename = filename + args.config_file

    # parse heuristic config file
    heuristic_config = parse_heuristic_config_file(filename, 'heuristic-params')
    logging.info('Heuristic configuration: ' + str(heuristic_config))

    round = -1
    while True:
        time.sleep(60)

        round += 1
        logging.info('Starting round ' + str(round))

        # read metrics database
        metrics_db = read_metrics(lines_num=lines_to_read)

        # get slicing associations
        slice_users = get_slice_users(metrics_db)

        # get some metric averages
        avg_dl_buffer_bytes = average_metric(metrics_db, dl_buffer_keyword)
        avg_dl_thr_mbps = average_metric(metrics_db, dl_thr_keyword)

        # sum metrics over slice
        slice_metrics = dict()
        for key, val in slice_users.items():
            slice_metrics[key] = {users_num_keyword: len(val)}

        # average slice metrics into dict
        avg_slice_metrics(slice_metrics, slice_users, avg_dl_buffer_bytes, dl_buffer_keyword)
        avg_slice_metrics(slice_metrics, slice_users, avg_dl_thr_mbps, dl_thr_keyword)

        logging.info('slice_metrics:')
        logging.info(slice_metrics)

        implement_heuristic(heuristic_config, slice_metrics, users_num_keyword, dl_buffer_keyword, dl_thr_keyword)
