import argparse
import ast
import collections
import json
import logging
import os
import time

from heuristic import parse_heuristic_config_file
from scope_start import write_tenant_slicing_mask


if __name__ == '__main__':

    time_freq_s = 30

    # Define command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('--config-file', type=str, required=True, help='Configuration file to parse.')
    args = parser.parse_args()

    # configure logger and console output
    logging.basicConfig(level=logging.DEBUG, filename='/logs/slice_heuristic.log', filemode='a+',
        format='%(asctime)-15s %(levelname)-8s %(message)s')
    formatter = logging.Formatter('%(asctime)-15s %(levelname)-8s %(message)s')
    console = logging.StreamHandler()
    console.setLevel(logging.INFO)
    console.setFormatter(formatter)
    logging.getLogger('').addHandler(console)

    base_config = config_path + 'radio_interactive.conf'

    # parse config file
    config_path = os.path.expanduser('~/radio_api/')
    slice_heuristic_config = config_path + args.config_file

    tenant_num = parse_heuristic_config_file(slice_heuristic_config, 'tenant-number')
    logging.info('Tenant number: ' + str(tenant_num))

    heuristic_config = parse_heuristic_config_file(slice_heuristic_config, 'slice-heuristic')
    logging.info('Heuristic configuration: ' + str(heuristic_config))

    # assemble config dict
    config_params = dict()
    config_params['network_slicing_enabled'] = True
    config_params['tenant_number'] = tenant_num

    # element 0 is already passed at the beginning, start from 1
    idx = 1
    while True:
        el_idx = idx % len(heuristic_config)
        el = heuristic_config[el_idx]
        time.sleep(time_freq_s)

        logging.info('Parsing configuration: ' + str(el))
        logging.info('idx ' + str(idx) + ', el_idx ' + str(el_idx))

        # add current slice configuration to config parameters
        config_params['slice_allocation'] = el

        # write slicing mask
        write_tenant_slicing_mask(config_params)

        idx += 1
