import collections
import copy
import csv
import enum
import math
import numpy as np
import logging
import os
import os.path
import re
import shutil
import subprocess

import constants
from support_functions import run_tmux_command

class SchedPolicy(enum.Enum):
    round_robin = 0
    waterfilling = 1
    proportionally = 2


# read srsLTE user database
def read_user_database(dir_path: str) -> list:

    # expected number of fields in srsLTE user database
    # Set it based on release_20_04 and update it when
    # reading actual lines to adapt to current db
    num_db_fields = 10
    db_description_line = 'Kept in the following format'

    # read lines from user database
    user_db_path = dir_path + 'user_db.csv'
    db_file = open(user_db_path, 'r')
    db_lines = db_file.readlines()
    db_file.close()

    out_lines = list()

    # divide not commented lines in comma-separated lists
    # and save them in structure
    for line in db_lines:
        # get number of database fields and skip commented lines
        if line[0] == '#':
            if db_description_line in line:
                num_db_fields = len(line.split('"')[1].split(','))
            continue

        # skip lines with wrong number of fields
        if len(line.split(',')) < num_db_fields:
            continue

        out_lines.append(line.split(','))

    return out_lines


# get mapping of srsLTE UE IPs and Colosseum tr0 ip_base
# read srsLTE static IPs from EPC database configu file user_db.csv
# NOTE: skip myself
def get_srsue_ip_mapping(bs_id: int, nodes_ip: dict, srslte_config_dir: str) -> tuple:

    # key: tr0 IP, value: srsLTE IP
    srs_col_ip_mapping = dict()

    # key: imsi, srsLTE IP
    srs_imsi_id_mapping = dict()

    # # read lines from user database
    # user_db_path = os.path.expanduser('~/radio_code/srslte_config/user_db.csv')
    # db_file = open(user_db_path, 'r')
    # db_lines = db_file.readlines()
    # db_file.close()

    db_lines = read_user_database(srslte_config_dir)

    # get srsLTE IPs
    for line in db_lines:
        # get srsLTE user ID, remove the 'ue' part at the beginning and whitespaces
        srs_ue_id = [x for x in line if re.search('^ue{1}([0-9]+)$', x)][0]
        srs_ue_id = int(re.sub('^ue{1}', '', srs_ue_id))

        # skip base station
        if srs_ue_id == bs_id:
            continue

        # add offset to srsLTE ue ID (see function description for main idea)
        srs_ue_id += bs_id - 1

        # get line corresponding to new srs_ue_id
        for inner_line in db_lines:
            inner_srs_ue_id = [x for x in inner_line if re.search('^ue{1}([0-9]+)$', x)][0]
            inner_srs_ue_id = int(re.sub('^ue{1}', '', inner_srs_ue_id))

            if srs_ue_id == inner_srs_ue_id:
                user_line = inner_line
                break

        # find srs_ue_ip and strip '\n' that may be there
        srs_ue_ip = [x for x in user_line if re.search('^172\.16\.0\.([0-9]{1,3})$', x)][0].strip()

        # get imsi
        srs_ue_imsi = user_line[2]

        try:
            if nodes_ip is not None:
                srs_col_ip_mapping[nodes_ip[srs_ue_id]] = srs_ue_ip
            srs_imsi_id_mapping[srs_ue_imsi] = srs_ue_id
        except KeyError:
            # skip keys of users not in the network
            pass

    return srs_col_ip_mapping, srs_imsi_id_mapping


# start EPC or eNB apps with inside tmux sessions
# node_type: epc, enb, ue. Optionally log output in log_path, ignored if empty
def start_srslte(tmux_session_name: str, srslte_config_dir: str, node_type: str, log_path: str) -> None:

    logging.info('Starting node: ' + node_type + '...')

    # configuration file path
    srslte_config_file = node_type + '.conf'
    srslte_cmd = 'cd ' + srslte_config_dir + ' && srs' + node_type + ' ' +\
        srslte_config_dir + srslte_config_file

    if len(log_path) > 0:
        logging.info('Logging output in ' + log_path)
        srslte_cmd += '  | tee ' + log_path

    run_tmux_command(srslte_cmd, tmux_session_name)


# configure srsLTE UE config file based on my ID and user database
def setup_srsue_config(my_node_id: int, srslte_config_dir: str) -> None:

    ue_config_file = 'ue.conf'
    ue_config_file_path = srslte_config_dir + ue_config_file
    backup_file = ue_config_file_path + '.bak'

    # define indices of user.db lines elements based on srsLTE release_20_04
    # this DB is supposed to be the same at BS and UE sides
    db_structure = {'ue_id': 0,
                    'auth_algo': 1,
                    'imsi': 2,
                    'ue_key': 3,
                    'op_type': 4,
                    'op_opc': 5}

    # define structure of ue.conf file based on srsLTE release_20_04
    usim_start = '[usim]'

    # key of ue config structure are supposed to be the same as
    # those of the db_structure above
    ue_conf_structure = {'auth_algo': 'algo',
                         'op_opc': 'opc',
                         'ue_key': 'k',
                         'imsi': 'imsi'}

    # convert to ordered disctionary so ue.conf is not changed at every run
    ue_conf_structure = collections.OrderedDict(sorted(ue_conf_structure.items()))

    # read user database
    db_lines = read_user_database(srslte_config_dir)

    ue_found = False

    for line in db_lines:
        # get UE ID
        srs_ue_id = int(re.sub('^ue{1}', '', line[db_structure['ue_id']]))

        # substitute fields in the user configuration file
        if srs_ue_id == my_node_id:

            ue_found = True

            logging.info('Writing ' + ue_config_file + ' file for ue' + str(my_node_id) + '...')

            # backup current config file
            os.system('mv ' + ue_config_file_path + ' ' + backup_file)

            try:
                # read lines from old file and write them on new file
                with open(backup_file, 'r') as in_file, open(ue_config_file_path, 'w') as out_file:

                    usim_section = False

                    in_line = in_file.readline()

                    while in_line:
                        # directly write lines before field_start
                        if not usim_section:
                            out_file.write(in_line)

                            if usim_start in in_line:
                                usim_section = True

                                # write mode that is always soft
                                out_file.write('mode = soft\n')

                        # do this before updating variable to skip line with variable only
                        else:
                            for key, value in ue_conf_structure.items():
                                out_line = value + ' = ' + line[db_structure[key]] + '\n'

                                out_file.write(out_line)

                                # consume in_line corresponding to the written line
                                in_file.readline()

                            # wrote all parameters, set variable to False
                            usim_section = False

                        # get next line
                        in_line = in_file.readline()
            except:
                # restore backup file if anything went wrong
                logging.error('Error while writing ' + ue_config_file + '. Restoring backed up file')
                os.system('mv ' + backup_file + ' ' + ue_config_file_path)
                raise

            # remove backed up file
            os.system('rm ' + backup_file)
            logging.info('Written ' + ue_config_file + ' file for ue' + str(my_node_id))
            break

    if not ue_found:
        logging.error('ue' + str(my_node_id) + ' not found in ' + ue_config_file + ' file. User file not configured')


# write configuration parameters on file
def write_config_params(config_params: dict) -> None:

    path = constants.SCOPE_CONFIG
    filename = 'scope_cfg.txt'
    path += filename

    logging.info('Writing configuration parameters on ' + path)

    # convert config file parameters in the right format
    params_to_write = ['colosseum_testbed', 'global_scheduling_policy', 'force_dl_modulation', 'network_slicing_enabled']

    delimiter = '::'

    # sort dictionary
    params_dict = collections.OrderedDict(sorted(config_params.items()))

    with open(path, 'w') as file:
        for key, value in params_dict.items():
            if key not in params_to_write:
                continue

            logging.info(key + delimiter + str(int(value)))
            # str(int(value)) is used to convert booleans to 0 and 1
            file.write(key + delimiter + str(int(value)) + '\n')


# write user slice association in loop
# e.g., in case of 4 slices write 0-1-2-3-0-1-2-3...
def write_imsi_slice(config_params: dict, imsi_id_mapping: dict) -> None:

    path = constants.SCOPE_CONFIG + 'slicing/'
    filename = 'ue_imsi_slice.txt'
    in_file = path + filename
    out_file = in_file + '.out'

    default_slice = 0

    if config_params['custom_ue_slice']:
        print(config_params['slice_users'])

    if config_params['network_slicing_enabled']:
        if config_params['custom_ue_slice']:
            logging.info('Writing custom IMSI-slice association on ' + in_file)
        else:
            logging.info('Writing cyclic IMSI-slice association on ' + in_file)
    else:
        logging.info('Network slicing disabled. Not writing IMSI-slice association')
        return

    delimiter = '::'
    num_tenants = int(config_params['tenant_number'])

    # get lines from input file
    with open(in_file, 'r') as f_in:
        in_lines = [line.rstrip() for line in f_in]

    curr_slice = 0
    with open(out_file, 'w') as f_out:
        for line in in_lines:
            imsi = line.split(delimiter)[0]

            # use UE-slice association passed in config file
            if config_params['custom_ue_slice']:
                try:
                    traffic_node_id = imsi_id_mapping[imsi]

                    # find slice of current node
                    curr_slice = -1
                    for key, val in config_params['slice_users'].items():
                        if traffic_node_id in val:
                            curr_slice = key

                    # default in case it was not found
                    if curr_slice == -1:
                        curr_slice = default_slice

                except KeyError:
                    curr_slice = default_slice
            else:
                # use cyclic UE-slice association
                if curr_slice >= num_tenants:
                    curr_slice = 0

            new_line = imsi + delimiter + str(curr_slice) + '\n'
            f_out.write(new_line)

            if not config_params['custom_ue_slice']:
                curr_slice += 1

    # substitute old file with new file
    shutil.move(out_file, in_file)


# write tenant resource allocation on file
def write_tenant_slicing_mask(config_params: dict, full_mask: bool=False, slice_idx: int=-1, ul: bool=False) -> None:

    # total number of RBGs srsLTE expects to read and
    # number used with the current configuration
    rbg_tot = 25
    rbg_available = 25

    # number of rows to write on slicing mask file
    rows_to_write = 1

    path = constants.SCOPE_CONFIG + 'slicing/'
    filename = 'slice_allocation_mask_tenant_'
    if ul:
        filename = 'ul_' + filename
    out_file = path + filename

    if config_params['network_slicing_enabled']:
        logging.info('Writing slicing masks')
    else:
        logging.info('Network slicing disabled. Not writing slicing masks')
        return

    num_tenants = int(config_params['tenant_number'])

    if config_params['slice_allocation']:
        if not full_mask:
            passed_slice_allocation(config_params['slice_allocation'], rbg_available, rbg_tot, num_tenants, rows_to_write, out_file)
        elif full_mask and slice_idx > -1:
            write_full_slice_mask(slice_idx, config_params['slice_allocation'], rows_to_write, out_file)
    else:
        # equally divide resources among slices
        equal_slice_allocation(rbg_available, rbg_tot, num_tenants, rows_to_write, out_file)


# write slice mask 'as is'
def write_full_slice_mask(slice_idx: int, slice_mask: str, rows_to_write: int, out_file: str) -> None:

    logging.info('Writing full mask for slice ' + str(slice_idx) + ': ' + slice_mask)

    filename = out_file + str(slice_idx) + '.txt'

    # write mask on file
    with open(filename, 'w') as f:
        logging.info('Slicing mask tenant ' + str(slice_idx) + ': ' + slice_mask)
        for r_idx in range(rows_to_write):
            f.write(slice_mask + '\n')


# write passed slice allocation
def passed_slice_allocation(slice_alloc: dict(), rbg_available: int, rbg_tot: int, num_tenants: int, rows_to_write: int, out_file: str) -> None:

    logging.info('Assigning passed slice resources')

    # copy input dictionary
    s_alloc = copy.deepcopy(slice_alloc)

    # check we are not forgetting any slice
    for s_idx in range(num_tenants):
        if s_alloc.get(s_idx) is None:
            # insert with allocation [-1, -1]
            # that will trigger an empty 0 allocation in next for cycle
            s_alloc[s_idx] = [-1, -1]

    for s_key, s_val in s_alloc.items():
        mask = ''

        rbg_min = s_val[0]
        rbg_max = s_val[1]

        for rbg_idx in range(rbg_tot):
            if rbg_idx < rbg_available and rbg_idx >= rbg_min and rbg_idx <= rbg_max and (rbg_min > -1 and rbg_max > -1):
                mask += '1'
            else:
                mask += '0'

        filename = out_file + str(s_key) + '.txt'

        # do not write empty [-1, -1] mask if file already
        if os.path.isfile(filename) and rbg_min == rbg_max == -1:
            continue

        # write mask on file
        with open(filename, 'w') as f:
            logging.info('Slicing mask tenant ' + str(s_key) + ': ' + mask)
            for r_idx in range(rows_to_write):
                f.write(mask + '\n')

    logging.info('All slicing masks written on configuration files')


def equal_slice_allocation(rbg_available: int, rbg_tot: int, num_tenants: int, rows_to_write: int, out_file: str) -> None:

    logging.info('Equally assigning slice resources')

    # equally divide RBGs among tenants
    rbg_tenant = math.floor(rbg_available / num_tenants)

    # initialize empty slicing mask
    mask_template = ''
    for i in range(rbg_tot):
        mask_template += '0'

    # dictionary to store current masks
    mask_dict = dict()

    # create slicing mask
    for s_idx in range(num_tenants):
        rbg_assigned = 0
        curr_mask = mask_template

        # get list of available RBG indices
        free_rbg_list = list(range(rbg_tot))

        if mask_dict:
            # remove RBGs already assigned for tenants already processed
            for _, oth_mask in mask_dict.items():
                mask_taken = [m.start() for m in re.finditer('1', oth_mask)]

                # remove taken elements from list
                for el in mask_taken:
                    try:
                        free_rbg_list.remove(el)
                    except ValueError:
                        # element has already been removed in previous iterations, pass
                        pass

        # sort list so we assign indices sequentially
        free_rbg_list.sort()

        # assign RBGs to current tenant
        while rbg_assigned < rbg_tenant:
            curr_mask = replace_slicing_mask_bit(curr_mask, '1', free_rbg_list[rbg_assigned], rbg_tot)
            rbg_assigned += 1

        # assign one extra bit to first and last tenants in case num_tenants is 3
        # logic behind this: first tenant suffers from control channel,
        # last tenant gets assignes last RBG which is usually shorter
        if num_tenants == 3:
            if s_idx == 0:
                curr_mask = replace_slicing_mask_bit(curr_mask, '1', rbg_assigned, rbg_tot)
            elif s_idx == num_tenants - 1:
                curr_mask = replace_slicing_mask_bit(curr_mask, '1', free_rbg_list[rbg_assigned], rbg_tot)

        # insert mask in dictionary
        mask_dict[s_idx] = curr_mask

        logging.info('Slicing mask tenant ' + str(s_idx) + ': ' + curr_mask)

        # write mask on file
        with open(out_file + str(s_idx) + '.txt', 'w') as f:
            for r_idx in range(rows_to_write):
                f.write(mask_dict[s_idx] + '\n')

    logging.info('All slicing masks written on configuration files')


# write new slicing mask based on increment or decrement of current resources
def write_slice_allocation_relative(slice_idx: int, slice_rbg: int) -> None:

    rbg_tot = 25
    base_filename = constants.SCOPE_CONFIG + 'slicing/slice_allocation_mask_tenant_'

    curr_mask = [int(x) for x in list(read_slice_mask(slice_idx))]
    curr_rbg = sum(curr_mask)

    if curr_rbg == slice_rbg:
        logging.info('No change needed in slice allocation of slice ' + str(slice_idx))
        return

    # read allocation of all slices and find free indexes
    all_masks = list()
    for s_idx in range(constants.SLICE_NUM):
        tmp_mask = [int(x) for x in list(read_slice_mask(s_idx))]

        if len(all_masks) == 0:
            all_masks = tmp_mask
        else:
            all_masks = [min(tmp_mask[i] + all_masks[i], 1) for i in range(len(all_masks))]

    # find index of free bits
    free_rbg_idx = list(np.where(np.array(all_masks) == 0)[0])

    # find allocated indexes
    alloc_rbg_idx = list(np.where(np.array(curr_mask) == 1)[0])

    # check if free space to increase resources
    if curr_rbg < slice_rbg:
        logging.info('Increasing RBG slice ' + str(slice_idx))

        right_done = False
        next_idx = min(alloc_rbg_idx) + 1
        while curr_rbg < slice_rbg:
            # check if there are no more free indices or
            # if we reached the maximum number of allocated RBGs
            if len(free_rbg_idx) == 0 or min(free_rbg_idx) >= constants.MAX_RBG:
                logging.info('No more resources available')
                print('No more resources available')
                break

            # replace bit in mask
            if next_idx in free_rbg_idx:
                tmp_mask = replace_slicing_mask_bit(''.join([str(x) for x in curr_mask]), '1', next_idx, rbg_tot)
                curr_mask = [int(x) for x in tmp_mask]
                curr_rbg = sum(curr_mask)
                free_rbg_idx.remove(next_idx)

            # move next idx
            # start from min allocated index and look for free indices to the right, then to the left
            if not right_done:
                next_idx += 1
                if next_idx >= constants.MAX_RBG:
                    right_done = True
                    next_idx = min(alloc_rbg_idx) - 1
            else:
                next_idx -= 1
                if next_idx <= 0:
                    logging.info('No more resources available')
                    print('No more resources available')
                    break
    else:
        # decrease resources from the right
        logging.info('Decreasing RBG slice ' + str(slice_idx))

        while curr_rbg > slice_rbg:
            if len(alloc_rbg_idx) == 0:
                logging.info('No more resources available')
                print('No more resources available')
                break

            next_idx = max(alloc_rbg_idx)
            tmp_mask = replace_slicing_mask_bit(''.join([str(x) for x in curr_mask]), '0', next_idx, rbg_tot)
            curr_mask = [int(x) for x in tmp_mask]
            curr_rbg = sum(curr_mask)
            alloc_rbg_idx = list(np.where(np.array(curr_mask) == 1)[0])

    # write new slicing mask
    curr_mask = ''.join([str(x) for x in curr_mask])    
    write_full_slice_mask(slice_idx, curr_mask, 1, base_filename)


# write slicing scheduling policy on configuration file
def write_slice_scheduling(config_params: dict) -> None:

    path = constants.SCOPE_CONFIG + 'slicing/'
    filename = 'slice_scheduling_policy.txt'
    in_file = path + filename
    out_file = in_file + '.out'

    if config_params['network_slicing_enabled']:
        logging.info('Writing slice scheduling policies')
    else:
        logging.info('Network slicing disabled. Not writing slice scheduling policies')
        return

    delimiter = '::'

    # get lines from input file
    with open(in_file, 'r') as f_in:
        in_lines = [line.rstrip() for line in f_in]

    # write output file
    with open(out_file, 'w') as f_out:
        for line in in_lines:
            # copy comment lines
            if line[0] == '#':
                line_to_write = line
            else:
                # get slice number and policy
                s_idx = int(line.split(delimiter)[0])
                s_policy = config_params['slice_scheduling_policy'][s_idx]

                line_to_write = str(s_idx) + delimiter + str(s_policy)

            f_out.write(line_to_write + '\n')

    # substitute old file with new file
    shutil.move(out_file, in_file)

    logging.info(config_params['slice_scheduling_policy'])


# read slice-scheduling policies for each slice into list
def read_slice_scheduling() -> list:

    path = constants.SCOPE_CONFIG + 'slicing/'
    filename = 'slice_scheduling_policy.txt'
    in_file = path + filename

    out_list = list()

    delimiter = '::'

    # get lines from input file
    with open(in_file, 'r') as f_in:
        in_lines = [line.rstrip() for line in f_in]

    for line in in_lines:
        # skip comment lines
        if line[0] == '#':
            continue

        out_list.append(int(line.split(delimiter)[1]))

    return out_list


# read mask of passed slice
def read_slice_mask(slice_idx: int, ul: bool = False) -> str:

    path = constants.SCOPE_CONFIG + 'slicing/'
    filename = 'slice_allocation_mask_tenant_'
    if ul:
        filename = 'ul_' + filename
    in_file = path + filename + str(slice_idx) + '.txt'

    try:
        with open(in_file, 'r') as f:
            mask = f.readline()
    except FileNotFoundError:
        mask = ''

    return mask.rstrip()


# replace slicing mask bit based on index
def replace_slicing_mask_bit(slicing_mask: str, new_bit: str, bit_idx: int, rbg_tot: int) -> str:

    first_half = slicing_mask[:bit_idx]

    if bit_idx == rbg_tot - 1:
        second_half = ''
    else:
        second_half = slicing_mask[bit_idx + 1:]

    return first_half + new_bit + second_half


# read metrics file based on user imsi
# read metrics for all users if ue_imsi is not specified
def read_metrics(lines_num: int=None, ue_imsi: str=None) -> collections.OrderedDict():

    path = constants.SCOPE_CONFIG + 'metrics/csv/'
    file_extension = '.csv'
    filename = '_metrics' + file_extension

    output_dict = collections.OrderedDict()
    min_imsi_len = 10

    if ue_imsi:
        output_dict[ue_imsi] = dict()
    else:
        # get files in directory
        dir_list = os.listdir(path)

        # only keep metric files such that IMSI length > min_imsi_len, other files are temporary RNTIs
        for el in dir_list:
            # ignore files starting with dot (.) and files that are not csv
            if os.path.splitext(el)[1] != file_extension or el[0] == '.':
                continue

            if filename in el:
                curr_imsi = el.split(filename)[0]

                if len(curr_imsi) >= min_imsi_len:
                    # insert imsi in dict
                    output_dict[curr_imsi] = collections.OrderedDict()

    # read csv files
    ts_keyword = 'Timestamp'
    for imsi in output_dict:
        metrics_file = path + imsi + filename
        metrics = read_csv(metrics_file, lines_num)

        # continue if file was not found
        if not metrics:
            continue

        # insert metrics in dictionary, use timestamp as inner key
        header = metrics[0]

        for row in metrics[1:]:
            # form inner dict with metric values
            row_dict = collections.OrderedDict()
            for hdr_idx, hdr_val in enumerate(header):
                # skip timestamp
                if not hdr_val:
                    continue

                row_dict[hdr_val] = row[hdr_idx]

            # insert inner dict in outer dict
            curr_timestamp = row[header.index(ts_keyword)]
            output_dict[imsi][curr_timestamp] = row_dict

    return output_dict


# read specified number of lines from bottom of csv file,
# read all if lines_num is not specified
def read_csv(filename: str, lines_num: int=None):

    output_list = list()

    try:
        with open(filename, 'r') as file:
            reader = csv.reader(file)
            for row in reader:
                output_list.append(row)

        # pop element to keep the number of specified lines + header
        if lines_num:
            while len(output_list) > lines_num + 1:
                output_list.pop(1)
    except FileNotFoundError:
        logging.error('read_csv: FileNotFound: ' + filename)

    return output_list


# get value from metrics dictionary for each saved timestamp
# get for all users if ue_imsi is not specified
def get_metric_value(metrics_dict: collections.OrderedDict(), metric_name: str, ue_imsi: str=None) -> collections.OrderedDict():

    output_dict = collections.OrderedDict()

    ts_keyword = 'Timestamp'
    imsi_keyword = 'IMSI'

    for imsi_key, val in metrics_dict.items():
        # skip entries different from ue_imsi if this parameter was specified
        if ue_imsi and imsi_key != ue_imsi:
            continue

        inner_dict = collections.OrderedDict()

        for ts_key, metric_val in  val.items():
            inner_dict[ts_key] = collections.OrderedDict()

            try:
                inner_dict[ts_key][metric_name] = metric_val[metric_name]
            except KeyError:
                logging.error('get_metric_value: KeyError: ' + metric_name)
                return output_dict

        output_dict[imsi_key] = inner_dict

    return output_dict


# get number of users in each slice
def get_slice_users(metrics_dict: collections.OrderedDict()) -> dict():

    slice_keyword = 'slice_id'
    slice_user = dict()

    # read last couple of lines from config files
    user_db = get_metric_value(metrics_dict, slice_keyword)

    for imsi_key, imsi_val in user_db.items():
        for _, ts_val in imsi_val.items():
            # get user slice
            curr_slice = int(ts_val[slice_keyword])

            # create new set if dict entry is empty and add imsi
            if slice_user.get(curr_slice) is None:
                slice_user[curr_slice] = set()
            slice_user[curr_slice].add(imsi_key)

            # break to only read the first line
            break

    return slice_user


# write bs config on file
def write_srslte_config(srslte_config_dir: str, param_dict: dict, is_bs: bool) -> None:

    # get filename of either bs or ue
    if is_bs:
        filename = 'enb.conf'
    else:
        filename = 'ue.conf'

    path = srslte_config_dir + filename

    for key, val in param_dict.items():
        write_srslte_param(path, key, str(val))

    # log current config file
    logging.info('Logging final configuration file (not printed to console)')

    os.system('echo "" >> /logs/run.log && ' + \
              'cat ' + path + ' >> /logs/run.log && ' + \
              'echo "" >> /logs/run.log')


# write srslte param on config file
def write_srslte_param(path: str, param_name: str, param_value: str) -> None:

    logging.info('Overriding ' + param_name + ' = ' + str(param_value) + ' on ' + path)

    param_regex = '\(^#\?' + param_name + '\s*=\s*[[:alnum:]]\+\s*$\)'
    cmd_param = "sed -i 's/" + param_regex + "/" + param_name + " = " + str(param_value) + "/g' " + path
    os.system(cmd_param)


# NOTE: legacy function, use write_srslte_config
# write parameters on enb.conf file
def write_srsenb_config(srslte_config_dir: str, dl_prb: int, dl_freq: int, ul_freq: int) -> None:

    filename = 'enb.conf'
    path = srslte_config_dir + filename

    # write PRB configuration
    if dl_prb > 0:
        logging.info('Writing dl_prb configuration on ' + path)

        dl_prb_regex = '\(^n_prb\s*=\s*[0-9]\+\s*$\)'
        cmd_dl_prb = "sed -i 's/" + dl_prb_regex + "/n_prb = " + str(dl_prb) + "/g' " + path
        os.system(cmd_dl_prb)
    else:
        logging.info('dl-prb not valid or not specified. Leaving value already in config file.')

    # write DL and UL frequency configurations
    write_frequency_config(path, 'dl_freq', dl_freq)
    write_frequency_config(path, 'ul_freq', ul_freq)


# NOTE: legacy function, use write_srslte_config
# write parameters on ue.conf file
def write_srsue_config(srslte_config_dir: str, dl_freq: int, ul_freq: int) -> None:

    filename = 'ue.conf'
    path = srslte_config_dir + filename

    # write DL and UL frequency configurations
    write_frequency_config(path, 'dl_freq', dl_freq)
    write_frequency_config(path, 'ul_freq', ul_freq)


# NOTE: legacy function, use write_srslte_config
# write frequency configuration
def write_frequency_config(filepath: str, freq_name: str, freq_val: int) -> None:

    logging.info('Writing ' + freq_name + ' = ' + str(freq_val) + ' on ' + filepath)

    freq_regex = '\(^' + freq_name + '\s*=\s*[0-9]\+\s*$\)'
    cmd_freq = "sed -i 's/" + freq_regex + "/" + freq_name + " = " + str(freq_val) + "/g' " + filepath
    os.system(cmd_freq)


# write single configuration parameter
def write_config_param_single(param: str, value: int, filename: str) -> None:

    path = constants.SCOPE_CONFIG
    path += filename

    sed_cmd = 'sed -i "s/^' + param + '.*/' + param + '::' + str(value) + '/g" ' + path
    os.system(sed_cmd)


# enable network slicing globally
def enable_slicing() -> None:
    write_config_param_single('network_slicing_enabled', 1, 'scope_cfg.txt')


# disable network slicing globally
def disable_slicing() -> None:
    write_config_param_single('network_slicing_enabled', 0, 'scope_cfg.txt')


# enable forcing of downlink mcs
def enable_dl_mcs_forcing() -> None:
    write_config_param_single('force_dl_modulation', 1, 'scope_cfg.txt')


# disable forcing of downlink mcs
def disable_dl_mcs_forcing() -> None:
    write_config_param_single('force_dl_modulation', 0, 'scope_cfg.txt')


# set global scheduling policy
def set_scheduling(policy_num: int) -> None:
    write_config_param_single('global_scheduling_policy', policy_num, 'scope_cfg.txt')


# set slice-users associations
# imsi_slice_dict: {user_imsi: slice}
def set_slice_users(imsi_slice_dict: dict()) -> None:

    path = 'slicing/ue_imsi_slice.txt'

    for imsi_key, slice_val in imsi_slice_dict.items():
        write_config_param_single(imsi_key, slice_val, path)


# set slice RBG resources for a single slice
def set_slice_resources(slice_idx: int, slice_mask: str, ul: bool=False) -> None:

    path = constants.SCOPE_CONFIG
    if ul:
        filename = 'slicing/ul_slice_allocation_mask_tenant_'
    else:
        filename = 'slicing/slice_allocation_mask_tenant_'
    path += filename

    write_full_slice_mask(slice_idx, slice_mask, 1, path)


# set scheduling for a single slice
def set_slice_scheduling(slice_idx: int, sched_policy: SchedPolicy) -> None:
    write_config_param_single(str(slice_idx), sched_policy.value, \
        'slicing/slice_scheduling_policy.txt')


# set scheduling policy for a given slice and
# assign slice resources relative to current mask
def set_slice(slice_idx: int, sched_policy: SchedPolicy, slice_rbg: int) -> None:

    set_slice_scheduling(slice_idx, sched_policy)
    write_slice_allocation_relative(slice_idx, slice_rbg)


# set downlink MCS for a given user
def set_mcs_dl(ue_imsi: str, ue_mcs: int) -> None:

    path = 'slicing/ue_imsi_modulation_dl.txt'
    write_config_param_single(ue_imsi, ue_mcs, path)


# set downlink power scaling factor for UE
def set_power(ue_imsi: str, scaling_factor: int) -> None:

    # get rnti and slice number from imsi
    metrics = read_metrics(2, ue_imsi)
    ue_rnti = list(get_metric_value(metrics, 'RNTI')[ue_imsi].items())[0][1]['RNTI']
    ue_slice = list(get_metric_value(metrics, 'slice_id')[ue_imsi].items())[0][1]['slice_id']

    path = 'config/ue_config_power_multiplier_slice_' + str(ue_slice) + '.txt'
    write_config_param_single(ue_rnti, scaling_factor, path)


# copy default/colosseum rr.conf and sib.conf
def copy_rr_sib_drb_conf(generic_testbed: bool) -> None:

    dst_dir = constants.RUNNING_CONFIG

    # if generic_testbed:
    #     logging.info('Copying default rr.conf, sib.conf, and drb.conf configuration files')
    #     src_dir = constants.GENERIC_CONFIG
    # else:
    #     logging.info('Copying Colosseum-specific rr.conf, sib.conf, and drb.conf configuration files')
    #     src_dir = constants.COLOSSEUM_CONFIG

    # always copy Colosseum configuration. It seems to work better
    logging.info('Copying rr.conf, sib.conf, and drb.conf configuration files')
    src_dir = constants.COLOSSEUM_CONFIG

    cpy_cmd = 'cp %s/rr.conf %s/; cp %s/sib.conf %s/; cp %s/drb.conf %s/' % (src_dir, dst_dir, src_dir, dst_dir, src_dir, dst_dir)
    os.system(cpy_cmd)


if __name__ == '__main__':
    set_power('1010123456002', 0.5)