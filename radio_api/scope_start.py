import argparse
import ast
import collections
import copy
import csv
import dill
import distutils.util
import json
import logging
import math
import netifaces as ni
import os.path
import random
import re
import shutil
import subprocess
import time

import constants
from scope_api import *
from support_functions import run_tmux_command

# get list of nodes in active reservation using colosseumcli rf radiomap
# NOTE: scenarios must have started already
# colosseumcli rf radiomap typical output is
# +--------+-----+--------+--------+
# | Node   | SRN | RadioA | RadioB |
# +--------+-----+--------+--------+
# | Node 1 |   5 |      1 |      2 |
# | Node 2 |   7 |      1 |      2 |
# | Node 3 |   8 |      1 |      2 |
# | Node 4 |   9 |      1 |      2 |
# +--------+-----+--------+--------+
def get_active_nodes_colosseumcli() -> dict:
    logging.info('Getting list of active nodes using colosseumcli')

    pipe = subprocess.Popen('colosseumcli rf radiomap', shell=True, stdout=subprocess.PIPE).stdout

    # separate lines returned by the above command
    lines = pipe.read().decode("utf-8").splitlines()

    # key: Node ID, value: SRN ID
    active_nodes = dict()

    # pipe character "|"" is currently used as a delimiter by colosseumcli
    delimiter = '|'

    # get nodes from list
    node_idx = -1
    srn_idx = -1
    for el in lines:
        logging.info(el)
        # only select lines with the delimiter
        if delimiter in el:
            # divide line by the delimiter and
            splitted_line = el.split(delimiter)

            # eliminate whitespaces and tabs
            tmp_list = list()
            for sp_el in splitted_line:
                # skip empty elements
                if len(sp_el) == 0:
                    continue

                tmp_list.append(sp_el.split())

            splitted_line = tmp_list

            # get position of "Node" and "SRN" keywords
            # in the first line of the table

            # find if current line is table heading
            # only do this if not already found
            is_heading = False
            if node_idx <= 0 and srn_idx <= 0:
                tmp_node_idx = -1
                tmp_srn_idx = -1
                for idx, sp_line_sublist in enumerate(splitted_line):
                    if 'Node' in sp_line_sublist:
                        tmp_node_idx = idx

                    if 'SRN' in sp_line_sublist:
                        tmp_srn_idx = idx

                    if 'RadioA' in sp_line_sublist:
                        is_heading = True
                        node_idx = tmp_node_idx
                        srn_idx = tmp_srn_idx

            # get active nodes if not heading
            if not is_heading:
                # skip None __string__ in case we are using a Colosseum scenario
                # with more nodes than what we have in the current reservation.
                # NOTE: None is returned as a string by Colosseum
                if splitted_line[srn_idx][0] != 'None':
                    active_nodes[int(splitted_line[node_idx][1])] = int(splitted_line[srn_idx][0])

    return active_nodes


# colosseumcli does not work in batch mode. Get nodes by scanning the
# collaboration network IPs through nmap and order them in a dictionary
# to get the same output returned by the get_active_nodes_colosseumcli function
def get_active_nodes_nmap() -> dict:

    logging.info('Getting list of active nodes using nmap')

    iface = 'col0'

    # get my col0 IP to figure out the subnetwork
    try:
        col0_ip = get_iface_ip(iface)
    except ValueError:
        logging.error('Interface ' + iface + ' is not active. Exiting execution')
        exit(1)

    logging.info(iface + ' IP address: ' + col0_ip)

    # get col0 base IP: e.g., if col0_ip is 172.30.104.103, get 172.30.104.
    col0_last = col0_ip.split('.')[3]
    col0_base_ip = col0_ip[:-len(col0_last)]

    logging.info(iface + ' base IP: ' + col0_base_ip)

    logging.info('Starting nmap')

    nmap_command = 'nmap -T4 -sn ' + col0_base_ip + '0/24'

    # SRN IP offset. E.g., SRN 1 has IP .101
    srn_offset = 100

    # flag to check we found at least one node that is not SRN.
    # Otherwise nmap has to be repeated
    nmap_successful = False
    re_runs = -1

    nmap_host_keyword = 'Nmap scan report for '
    nmap_up_keyword = 'Host is up'

    # key: Node ID, value: SRN ID
    active_nodes = dict()

    while not nmap_successful:
        # use a temporary list to store active nodes
        tmp_nodes = list()

        re_runs += 1

        if re_runs > 10:
            logging.error('No active hosts found by nmap. Exiting')
            exit(1)

        pipe = subprocess.Popen(nmap_command, shell=True, stdout=subprocess.PIPE).stdout

        # separate lines returned by the above command
        lines = pipe.read().decode("utf-8").splitlines()

        for l_idx in range(len(lines)):
            curr_line = lines[l_idx]

            # get SRN num from host IP
            if nmap_host_keyword in curr_line and nmap_up_keyword in lines[l_idx + 1]:
                srn_num = int(curr_line.split(col0_base_ip)[1]) - srn_offset

                # check this is an actual SRN and add it to dictionary
                if srn_num > 0:
                    tmp_nodes.append(srn_num)
                    logging.info('SRN ' + str(srn_num) + ' is active')
                else:
                    # this is not an SRN but is used to check that nmap is successful
                    logging.info('nmap successful')
                    nmap_successful = True

    if len(tmp_nodes) > 0:
        # sort list so the order is the same for all nodes
        tmp_nodes.sort()
        # store nodes into dictionary with position in list +1 as key
        # to have output compatible with that of the function using colosseumcli
        for idx, el in enumerate(tmp_nodes):
            active_nodes[idx + 1] = el
    else:
        logging.error('No active hosts found by nmap. Exiting')
        exit(1)

    logging.info('nmap completed!')

    return active_nodes


# get IP addresses of tr0 interface of active nodes
# in the reservation. Exclude my ip address
def get_nodes_ip(active_nodes: list) -> tuple:

    # key: Node ID, value: SRN IP address
    nodes_ip = dict()

    # get my IP address
    node_interface = 'tr0'
    my_ip = get_iface_ip(node_interface)

    # get my node id
    ip_offset = 100
    my_srn_id = int(my_ip.split('.')[2]) - ip_offset

    # build IP addresses of other nodes
    ip_base = '192.168.{}.1'
    for key in active_nodes:
        srn_id = active_nodes[key]

        # exclude myself
        if srn_id == my_srn_id:
            my_node_id = key
            continue

        nodes_ip[key] = ip_base.format(ip_offset + srn_id)

    return my_ip, my_node_id, nodes_ip


# elect base station nodes based on node ID and on bs_ue_num
# idea is as follows: If there are 3 nodes per base station,
# node 1 is BS, nodes 2, 3, 4 are UEs; node 5 is BS, nodes 6, 7, 8
# are UEs, and so on
def elect_bs(my_node_id: int, bs_ue_num: int) -> bool:
    return my_node_id % (bs_ue_num + 1) == 1


# find Colosseum Node ID of serving base station
def get_serving_bs_id(my_node_id: int, bs_ue_num: int) -> int:

    for node_idx in range(0, bs_ue_num):
        bs_id = my_node_id - node_idx - 1

        if elect_bs(bs_id, bs_ue_num):
            return bs_id

    logging.warning('Node ID of serving base station not found')

    return -1


# get Colosseum tr0 IP addresses of nodes I am serving
# same idea of elect_bs
def get_my_users_ip(my_node_id: int, bs_ue_num: int, nodes_ip: dict) -> dict:

    # key: node ID, value: tr0 IP
    my_users_ip = dict()

    for ue_idx in range(my_node_id + 1, my_node_id + bs_ue_num + 1):
        try:
            my_users_ip[ue_idx] = nodes_ip[ue_idx]
        except KeyError:
            # there are fewer nodes active than
            # what specified in the json file (users-bs). Just pass
            pass

    return my_users_ip


# print previously parsed configuration
def print_configuration(config: dict) -> None:

    # get length of longest key for printing purposes
    longest_word = sorted(config.keys(), key=len)[-1]

    logging.info('Parsed configuration:')
    for key in config:
        # add number of spaces needed
        print_key = add_tabs(key, longest_word)

        logging.info(print_key + str(config[key]))


# get configuration parameters from file
def get_config_params(config: dict) -> dict:

    max_tenants = 10

    logging.info('Getting configuration parameters...')

    config_params = dict()

    # convert config file parameters in the right format
    param_dict = {'colosseum-testbed': 'colosseum_testbed',
                  'global-scheduling-policy': 'global_scheduling_policy',
                  'force-dl-modulation': 'force_dl_modulation',
                  'force-ul-modulation': 'force_ul_modulation',
                  'network-slicing': 'network_slicing_enabled',
                  'slice-scheduling-policy': 'slice_scheduling_policy',
                  'slice-allocation': 'slice_allocation',
                  'tenant-number': 'tenant_number',
                  'custom-ue-slice': 'custom_ue_slice',
                  'slice-users': 'slice_users',
                  'bs-config': 'bs_config',
                  'ue-config': 'ue_config'}

    # get length of longest string for printing purposes
    longest_word = sorted(param_dict.keys(), key=len)[-1]

    parameter_found = False
    for param_key, param_value in param_dict.items():
        try:
            if param_key in ['slice-scheduling-policy', 'slice-allocation', 'slice-users', \
            'bs-config', 'ue-config']:
                # convert in python structure
                try:
                    passed_value = ast.literal_eval(config[param_key])
                except ValueError:
                    logging.info('')
                    logging.info('Did you remember to pass a configuration file? :-)\n')
                    raise

                if param_key == 'slice-scheduling-policy':
                    # add default policy to missing tenants
                    while len(passed_value) < max_tenants:
                        passed_value.append(0)
            else:
                passed_value = config[param_key]

            config_params[param_value] = passed_value
            parameter_found = True
        except KeyError:
            # add parameter for running on Colosseum
            if param_key == 'colosseum-testbed':
                config_params[param_value] = True
                parameter_found = True
            elif param_key in ['slice-allocation', 'bs-config', 'ue-config']:
                config_params[param_value] = dict()
                parameter_found = True
            elif param_key == 'custom-ue-slice':
                config_params[param_value] = False
                parameter_found = True
            else:
                parameter_found = False

        if parameter_found:
            # add number of spaces needed
            print_param = add_tabs(param_value, longest_word)
            logging.info(param_key + ' ' + str(config_params[param_value]))
        else:
            logging.warning('Parameter ' + param_key + ' not found. Using default value')

    return config_params


# add the right number of spaces for printing purposes
def add_tabs(input_string: str, longest_word: str) -> str:

    output_string = input_string
    desired_len = len((longest_word + '\t').expandtabs())

    while len(output_string.expandtabs()) < desired_len:
        output_string += ' '

    return output_string


# parse configuration file
def parse_config_file(filename: str) -> dict:

    with open(filename) as file:
        config = json.load(file)

    # convert to right types
    for key in config:
        if config[key].lower() in ['true', 'false']:
            config[key] = bool(distutils.util.strtobool(config[key]))
        elif key in ['users-bs',]:
            config[key] = int(config[key])

    return config


# get interface ip
# throws ValueError if the interface does not exist
def get_iface_ip(iface: str) -> str:

    return ni.ifaddresses(iface)[ni.AF_INET][0]['addr']


# capture packets on pcap file
def capture_pcap(iface: str, node_type: str) -> None:

    pcap_filename = '/logs/' + node_type + '_' + iface + '.pcap'

    # check if interface exists first, sleep for some time and retry if not
    iface_ip = ''
    max_trials = 10
    for i in range(max_trials):
        try:
            iface_ip = get_iface_ip(iface)
        except ValueError:
            time.sleep(5)

    if len(iface_ip) == 0:
        logging.warning('Interface ' + iface + ' not found. Not capturing packets')
        return

    # start tcpdump in the current tmux session and dump on file after receiving
    # every packet (-U option)
    tcpdump_command = 'tcpdump -v -i ' + iface + ' -U -w ' + pcap_filename
    run_tmux_command(tcpdump_command, 'tcpdump')
    # subprocess.Popen(tcpdump_command, shell=True)

    logging.info('Capturing ' + iface + ' packets in ' + pcap_filename)


# figure out if the current node is a base station
def is_node_bs(bs_ue_num: int, use_colosseumcli: bool) -> tuple:

    if use_colosseumcli:
        active_nodes = get_active_nodes_colosseumcli()
    else:
        active_nodes = get_active_nodes_nmap()

    logging.info('Active nodes: ' + str(active_nodes))

    # check that we have enough nodes in the reservation for the set
    # number of UEs per base stations. (-1: one node is the BS).
    # If no nodes are fond through colosseumcli, most likely the scenario has not been started
    if len(active_nodes.keys()) == 0 and use_colosseumcli:
        logging.error('No nodes found, exiting. ' + \
            'Did you start the Colosseum scenario (colosseumcli rf start <scenario-number> -c)? ' + \
            'If in batch mode, disable the colosseumcli option in the configuration file')
        exit(1)
    elif len(active_nodes.keys()) - 1 < bs_ue_num:
        logging.warning('Not enough active nodes in the reservation to accomodate ' + \
            str(bs_ue_num) + ' users per base station. Setting users per base station to: ' + \
            str(len(active_nodes.keys()) - 1))
        bs_ue_num = len(active_nodes.keys()) - 1

    my_ip, my_node_id, nodes_ip = get_nodes_ip(active_nodes)
    logging.info('My tr0 IP: ' + my_ip + ' , my node ID: ' + str(my_node_id))
    logging.info('Nodes IPs ' + str(nodes_ip))

    # elect if current node is a base station
    is_bs = elect_bs(my_node_id, bs_ue_num)

    return is_bs, my_ip, my_node_id, nodes_ip, bs_ue_num


# start iperf client in reverse mode
def start_iperf_client(tmux_session_name: str, server_ip: str, client_ip: str) -> None:

    default_port = 5201

    # derive port offset from my srsLTE IP
    port_offset = int(client_ip.split('.')[-1])
    port = default_port + port_offset

    # iperf_cmd = 'iperf3 -c ' + server_ip + ' -p ' + str(port) + ' -u -b 5M -t 600 -R'
    iperf_cmd = 'iperf3 -c ' + server_ip + ' -p ' + str(port) + ' -t 600 -R'

    # wrap command in while loop to repeat it if it fails to start
    # (e.g., if ue is not yet connected to the bs)
    loop_cmd = 'while ! %s; do sleep 5; done' % (iperf_cmd)

    logging.info('Starting iperf3 client toward: ' + iperf_cmd)
    run_tmux_command(loop_cmd, tmux_session_name)


# start iperf server in background
def start_iperf_server(client_ip) -> None:

    default_port = 5201

    for c_ip in client_ip:
        # derive port offset from client srsLTE IP
        port_offset = int(c_ip.split('.')[-1])
        port = default_port + port_offset
        logging.info('Starting iperf3 server in background on port ' + str(port))

        iperf_cmd = 'iperf3 -s -p ' + str(port) + ' -D'
        os.system(iperf_cmd)


# write scope configuration, srsLTE parameters and start cellular applicaitons
def run_scope(bs_ue_num: int, iperf: bool, use_colosseumcli: bool,
    capture_pkts: bool, config_params: dict, write_config_parameters: bool):

    # define name of the tmux session in which commands are run
    tmux_session_name = 'scope'
    srslte_config_dir = os.path.expanduser('~/radio_code/srslte_config/')

    # kill existing tmux sessions at startup
    os.system('kill -9 `pidof srsepc`')
    os.system('kill -9 `pidof srsenb`')
    os.system('kill -9 `pidof srsue`')
    os.system('kill -9 `pidof iperf3`')
    os.system('tmux kill-session -t ' + tmux_session_name)
    os.system('tmux kill-session -t tcpdump')

    is_bs, my_ip, my_node_id, nodes_ip, bs_ue_num = is_node_bs(bs_ue_num, use_colosseumcli)

    # default srsLTE base station IP from the BS perspective
    srslte_bs_ip = '172.16.0.1'

    if is_bs:
        logging.info('Starting base station configuration...')

        epc_log_file = os.path.expanduser('/logs/colosseum_epc.log')
        enb_log_file = os.path.expanduser('/logs/colosseum_enb.log')

        os.system('rm ' + epc_log_file)
        os.system('rm ' + enb_log_file)

        # write srsenb configuration
        write_srslte_config(srslte_config_dir, config_params['bs_config'], True)

        # write configuration parameters on file
        if write_config_parameters:
            write_config_params(config_params)
            write_tenant_slicing_mask(config_params)
            write_slice_scheduling(config_params)
        else:
            logging.info('Not writing configuration parameters on file')

        # interface generated by srsLTE at eNB side
        srslte_iface = 'srs_spgw_sgi'

        # get Colosseum IPs of users I am serving
        my_users_ip = get_my_users_ip(my_node_id, bs_ue_num, nodes_ip)
        logging.info('My users IPs ' + str(my_users_ip))

        # get mapping of srsLTE UE addresses and IPs
        srs_col_ip_mapping, srs_imsi_id_mapping = get_srsue_ip_mapping(my_node_id, my_users_ip, srslte_config_dir)
        logging.info('tr0/srs IP mapping ' + str(srs_col_ip_mapping))
        logging.info('ue/imsi mapping ' + str(srs_imsi_id_mapping))

        logging.info('My srsLTE IP ' + srslte_bs_ip)

        if write_config_parameters:
            write_imsi_slice(config_params, srs_imsi_id_mapping)

        # start srsLTE EPC app to create the srs_spgw_sgi virtual interface
        start_srslte(tmux_session_name, srslte_config_dir, 'epc', epc_log_file)

        # logging.info('Setting LTE transceiver state to ACTIVE')
        # os.system('echo "ACTIVE" > /tmp/LTE_STATE')

        # start srsLTE eNB in the same tmux session as before but in a separate window
        start_srslte(tmux_session_name, srslte_config_dir, 'enb', enb_log_file)

        if capture_pkts:
            # capture packets on pcap file
            capture_pcap('tr0', 'enb')
            capture_pcap(srslte_iface, 'enb')
        else:
            logging.info('Packet capture via tcpdump disabled')

        # start iperf clients
        if iperf:
            start_iperf_server(srs_col_ip_mapping.values())

    else:
        logging.info('Starting user configuration...')

        ue_log_file = os.path.expanduser('/logs/colosseum_ue.log')
        ue_connected = False

        # remove log file so that program reads log file of current execution
        os.system('rm ' + ue_log_file)

        # interface generated by srsLTE at UE side
        srslte_iface = 'tun_srsue'

        # find Colosseum Node ID of base station that is serving me
        bs_id = get_serving_bs_id(my_node_id, bs_ue_num)

        # get mapping of srsLTE UE addresses and IPs
        srs_col_ip_mapping, _ = get_srsue_ip_mapping(bs_id, nodes_ip, srslte_config_dir)

        # compute my srsLTE IP and extract it from the returned dictionary
        my_srslte_ip, _ = get_srsue_ip_mapping(bs_id, {my_node_id: my_ip}, srslte_config_dir)
        my_srslte_ip = my_srslte_ip[my_ip]
        logging.info('My srsLTE IP: ' + my_srslte_ip)

        bs_tr0 = nodes_ip[bs_id]
        logging.info('Serving BS ID: ' + str(bs_id) + ' serving BS tr0: ' + bs_tr0 + ' serving BS IP: ' + srslte_bs_ip)

        # if bs_id is 1, it will be missing from srs_col_ip_mapping, insert tr0 entry associated with srsLTE BS IP,
        # else replace IP currently assigned to bs_tr0 key
        srs_col_ip_mapping[bs_tr0] = srslte_bs_ip
        logging.info(srs_col_ip_mapping)

        # configure srsLTE UE config file based on my ID and user database
        setup_srsue_config(my_node_id, srslte_config_dir)

        # write srsue configuration
        write_srslte_config(srslte_config_dir, config_params['ue_config'], False)

        # start srsLTE UE in tmux session
        start_srslte(tmux_session_name, srslte_config_dir, 'ue', ue_log_file)

        if capture_pkts:
            # capture packets on pcap file
            if not iperf:
                capture_pcap('tr0', 'ue')
            capture_pcap(srslte_iface, 'ue')
        else:
            logging.info('Packet capture via tcpdump disabled')

        if iperf:
            sleep_time = 10
            logging.info('iPerf option detected, sleeping ' + str(sleep_time) + 's')
            time.sleep(sleep_time)

            start_iperf_client(tmux_session_name, srslte_bs_ip, my_srslte_ip)


if __name__ == '__main__':

    ul_freq_default = 980000000  # Hz
    dl_freq_default = 1020000000  # Hz
    n_prb_default = 50

    prb_values = [6, 15, 25, 50, 75, 100]

    # Define command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('--capture-pkts', help='Enable packet capture from the tr0, srs_spgw_sgi,\
     and tun_srsue interfaces and dump them on .pcap files through tcpdump', action='store_true')
    parser.add_argument('--config-file', type=str, default='', help='json-formatted configuration file file to parse.\
        The other arguments are ignored if config file is passed')
    parser.add_argument('--iperf', help='Generate traffic through iperf3, downlink only', action='store_true')
    parser.add_argument('--users-bs', type=int, default=3, help='Maximum number of users per base station')
    parser.add_argument('--colcli', help='Use colosseumcli APIs to get list of active nodes.\
        This parameter is specific to Colosseum and it is only available in interactive mode', action='store_true')
    parser.add_argument('--write-config-parameters', help='If enabled, writes configuration parameters on file. Done at the base station-side',\
        action='store_true')

    # configuration parameters
    # scheduling: 0 for round-robin, 1 for waterfilling, 2 for proportional
    parser.add_argument('--custom-ue-slice', help='Use UE-slice associations passed in the configuration file', action='store_true')

    # the following three can be embedded into bs-config/ue-config. Leave for legacy config files
    parser.add_argument('--dl-freq', type=int, default=dl_freq_default, help='Downlink frequency [Hz].')
    parser.add_argument('--ul-freq', type=int, default=ul_freq_default, help='Uplink frequency [Hz].')
    parser.add_argument('--dl-prb', type=int, default=n_prb_default, choices=prb_values, help='Downlink PRBs.')

    parser.add_argument('--bs-config', type=str, help='BS configuration file to override srsLTE options. \
        Format as json, e.g., {\'n_prb\': 50, \'dl_freq\': 980000000, \'ul_freq\': 1020000000}.')
    parser.add_argument('--ue-config', type=str, help='UE configuration file to override srsLTE options. \
        Format as json (see bs-config).')

    parser.add_argument('--force-dl-modulation', help='Force downlink modulation from base station to users', action='store_true')
    parser.add_argument('--force-ul-modulation', help='Force uplink modulation from base station to users', action='store_true')
    parser.add_argument('--global-scheduling-policy', type=int, default=0, choices=[0, 1, 2], help='Global MAC-layer scheduling policy. Used at base station side,\
        overruled by slice-dependent scheduling if network slicing is enabled.')
    parser.add_argument('--network-slicing', help='Enable network slicing. Used at base station side', action='store_true')
    parser.add_argument('--slice-allocation', type=str, help='Slice allocation in the form of {slice_num: [lower_rbg, upper_rbg], ...} (inclusive),\
        e.g., {0: [0, 3], 1: [5, 7]} to assign RBGs 0-3 to slice 0 and 5-7 to slice 1')
    parser.add_argument('--slice-users', type=str, help='Slice UEs in the form of {slice_num: [ue1, ue2, ...], ...},\
        e.g., {0: [1, 5], 1: [2, 3, 4]} to associate UEs 1 and 5 to slice 0 and UEs 2, 3 and 4 to slice 1. The UE IDs correspond to the SRNs \
        of the reservation from the base station onwards. E.g., if SRNs 3, 5, 7, 8 are reserved and `users-bs` is set to 3, the base station is SRN 3, \
        while SRNs 5, 7 and 8 are the 1st, 2nd and 3rd users, respectively')
    parser.add_argument('--slice-scheduling-policy', type=str, help='Slicing policy for each slice in the format [0, 0, 1, 2, ...]')
    parser.add_argument('--tenant-number', type=int, default=2, choices=range(1, 11), help='Number of tenants for network slicing.')
    args = parser.parse_args()

    # configure logger and console output
    logging.basicConfig(level=logging.DEBUG, filename='/logs/run.log', filemode='a+',
        format='%(asctime)-15s %(levelname)-8s %(message)s')
    formatter = logging.Formatter('%(asctime)-15s %(levelname)-8s %(message)s')
    console = logging.StreamHandler()
    console.setLevel(logging.INFO)
    console.setFormatter(formatter)
    logging.getLogger('').addHandler(console)

    if len(args.config_file) == 0:
        # insert values in config dictionary
        config = {'capture-pkts': args.capture_pkts,
                  'colosseumcli': args.colcli,
                  'iperf': args.iperf,
                  'users-bs': args.users_bs,
                  'write-config-parameters': args.write_config_parameters,
                  'network-slicing': args.network_slicing,
                  'global-scheduling-policy': args.global_scheduling_policy,
                  'slice-scheduling-policy': args.slice_scheduling_policy,
                  'tenant-number': args.tenant_number,
                  'slice-allocation': args.slice_allocation,
                  'custom-ue-slice': args.custom_ue_slice,
                  'dl-freq': args.dl_freq,
                  'ul-freq': args.ul_freq,
                  'dl-prb': args.dl_prb,
                  'force-dl-modulation': args.force_dl_modulation,
                  'force-ul-modulation': args.force_ul_modulation,
                  'slice-users': args.slice_users,
                  'bs-config': args.bs_config,
                  'ue-config': args.ue_config}
    else:
        # parse config file
        filename = os.path.expanduser('~/radio_api/' + args.config_file)
        config = parse_config_file(filename)

        if args.colcli:
            logging.info('use-colosseumcli overridden by CLI argument. Setting it to True')
            config['colosseumcli'] = args.colcli

        # check if key write-config-parameters was given
        if config.get('write-config-parameters') is None:
            logging.info('write-config-parameters not specified. Setting it to False')
            config['write-config-parameters'] = False

        if config.get('dl-prb') is None:
            config['dl-prb'] = n_prb_default
        else:
            config['dl-prb'] = int(config['dl-prb'])

            if config['dl-prb'] not in prb_values:
                config['dl-prb'] = n_prb_default
            else:
                logging.info('Setting BS to ' + str(config['dl-prb']) + ' PRBs.')

        if config.get('dl-freq') is None:
            config['dl-freq'] = dl_freq_default
        else:
            config['dl-freq'] = int(config['dl-freq'])

        if config.get('ul-freq') is None:
            config['ul-freq'] = ul_freq_default
        else:
            config['ul-freq'] = int(config['ul-freq'])

        if config.get('iperf') is None:
            config['iperf'] = False

    print_configuration(config)
    config_params = get_config_params(config)

    # copy legacy parameters into new bs-config and ue-config dictionaries
    if config_params['bs_config'].get('dl_freq') is None:
        config_params['bs_config']['dl_freq'] = config['dl-freq']
    if config_params['bs_config'].get('ul_freq') is None:
        config_params['bs_config']['ul_freq'] = config['ul-freq']
    if config_params['bs_config'].get('n_prb') is None:
        config_params['bs_config']['n_prb'] = config['dl-prb']

    if config_params['ue_config'].get('dl_freq') is None:
        config_params['ue_config']['dl_freq'] = config['dl-freq']
    if config_params['ue_config'].get('ul_freq') is None:
        config_params['ue_config']['ul_freq'] = config['ul-freq']

    run_scope(config['users-bs'], config['iperf'],
        config['colosseumcli'], config['capture-pkts'],
        config_params, config['write-config-parameters'])

    # set LTE transceiver state to active
    time.sleep(2)
    logging.info('Setting LTE transceiver state to ACTIVE')
    os.system('echo "ACTIVE" > /tmp/LTE_STATE')
