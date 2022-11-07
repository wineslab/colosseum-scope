# SCOPE

SCOPE is a development environment for softwarized and virtualized NextG cellular networks based on <a href="https://github.com/srsran/srsRAN" target="_blank">srsLTE</a> (now srsRAN).
It provides: (i) A ready-to-use portable open-source cellular container with flexible 5G-oriented functionalities; (ii) data collection tools, such as dataset generation functions for recording cellular performance and metrics, and for facilitating data analysis; (iii) a set of APIs to control and reprogram key functionalities of the full cellular stack at run time, without requiring redeploying the network, and (iv) an emulation environment with diverse cellular scenarios closely matching real-world deployments for precise prototyping NextG network solutions.
SCOPE has been prototyped and benchmarked on the <a href="https://www.colosseum.net" target="_blank">Colosseum</a> wireless network emulator, where an LXC container of SCOPE has been made available, and it is portable to LXC-enabled testbeds.

If you use SCOPE, its APIs or scenarios, please reference the following paper:
> L. Bonati, S. D'Oro, S. Basagni, and T. Melodia, <i>"SCOPE: An Open and Softwarized Prototyping Platform for NextG Systems,"
> </i> in Proceedings of ACM MobiSys, June 2021.
> <a href="https://ece.northeastern.edu/wineslab/papers/bonati2021scope.pdf" target="_blank">[pdf]</a> <a href="https://ece.northeastern.edu/wineslab/wines_bibtex/bonati2021scope.txt" target="_blank">[bibtex]</a>

This work was partially supported by the U.S. National Science Foundation under Grant CNS-1923789 and the U.S. Office of Naval Research, Grants N00014-19-1-2409 and N00014-20-1-2132.

## Structure

SCOPE directory structure is organized as follows

```
root 
|
└──radio_api
|   
└──radio_code
|  |
|  └──scope_config
|  |
|  └──srsGUI
|  |
|  └──srsLTE
|  |
|  └──srslte_config
```

### radio_api

The `radio_api` directory contains SCOPE API script, examplary scripts, configuration files, and support files for Colosseum.

Quick start on Colosseum with base configuration:
- `python3 scope_start.py --config-file radio_interactive.conf`
- Cellular applications will be started in a new `tmux` session named `scope`, attach with `tmux a -t scope`

- Main SCOPE API scripts:
    - `constants.py`: Constant parameters used by the remaining scripts
    - `scope_api.py`: APIs to interact with cellular base station
    - `scope_start.py`: Quick start script for running on Colosseum testbed: Parse configuration file, configure and start cellular applications (i.e., base station and core network, or user). If using the quick start script outside Colosseum some pieces might require minor adaptation, e.g., manually supplying the node list instead of leveraging the automatic node discovery. Note that this script generates runtime logs in the `/logs` directory. If running it on your local machine, please make sure that such directory exists, and that your user can write inside it
    - `support_functions.py`: Additional support functions
- Exemplary scripts (to be run at the base station):
    - `heuristic.py`: Read performance metrics from dataset and implement arbitrary heuristic policy. In this example, read downlink buffer and throughput relative to served users from performance dataset. Then assign more resources to slices if buffers are above threshold (policy slice 0), and throughput is above threshold (policy for slice 2). If more users are served in a single slice, also change the slice scheduling policy from round-robin to waterfilling.
    - `slice_heuristic.py`: Periodically modify number of PRBs allocated to the network slices.
- Configuration files:
    - `radio.conf`: Dummy configuration file replaced by Colosseum when running batch jobs
    - `radio_interactive.conf`: Exemplary configuration file to use with `scope_start.py` script
    - `heuristic.conf`: Exemplary configuration file to use with `heuristic.py` script
    - `slice_heuristic.conf`: Exemplary configuration file to use with `slice_heuristic.py` script
- Colosseum support files:
    - `start.sh`: Used by Colosseum to start a batch job
    - `statistics.sh`: Used by Colosseum to check on the radio performance
    - `status.sh`: Used by Colosseum to check on the radio state
    - `stop.sh`: Used by Colosseum to stop a batch job

#### SCOPE API configuration parameters

A list of the configuration parameters accepted by SCOPE APIs follows.

- `bs-config`/`ue-config`: Base station/UE configuration parameters to override those specified on srsLTE configuration files. Format as json, e.g., `{'n_prb': 50, 'dl_freq': 2655000000, 'ul_freq': 2535000000}`
- `capture-pkts`: Enable packet capture and dumps them on `.pcap` files through `tcpdump`. By default, the monitored interfaces are `srs_spgw_sgi`, `tun_srsue`, and Colosseum `tr0`
- `colcli`: Use `colosseumcli` APIs to get list of active nodes. This parameter is specific to Colosseum and it is only available in interactive mode
- `config-file`: JSON-formatted configuration file where to read these parameters from. The other arguments are ignored if a configuration file is passed
- `custom-ue-slice`: Use UE-slice associations passed in the configuration file
- `dl-freq`/`ul-freq`: Downlink/uplink frequency for base station and users [Hz]
- `dl-prb`: Number of downlink PRBs to use at the base station
- `force-dl-modulation`/`force-ul-modulation`: Force downlink/uplink modulation of base station/users
- `global-scheduling-policy`: Global MAC-layer scheduling policy. Used at base station side, overruled by slice-dependent scheduling if network slicing is enabled. Possible values are: `0`: Round-robin, `1`: Waterfilling, `2`: Proportionally fair
- `iperf`: Generate traffic through `iperf3`, downlink only
- `network-slicing`: Enable network slicing. Used at base station side
- `slice-allocation`: Base station slice allocation.<sup>[1](#footnote1)</sup> This is passed in the form of `{slice_num: [lowest_allowed_rbg, highest_allowed_rbg], ...}` (inclusive). E.g., `{0: [0, 3], 1: [5, 7]}` assigns RBGs 0-3 to slice 0 and 5-7 to slice 1
- `slice-scheduling-policy`: Slicing policy for each slice in a list format.<sup>[1](#footnote1)</sup> E.g., `[2, 0, 1, ...]` assigns policy 2 to slice 0, policy 0 to slice 1 and policy 1 to slice 2. Possible values are: `0`: Round-robin, `1`: Waterfilling, `2`: Proportionally fair 
- `slice-users`: Slice UEs in the form of `{slice_num: [ue1, ue2, ...], ...}`, e.g., `{0: [2, 6], 1: [3, 4, 5]}` associates UEs 2, 6 to slice 0 and UEs 3, 4, 5 to slice 1. The UE IDs correspond to the SRNs of the reservation (the first base station has ID equal to 1). E.g., if SRNs 3, 5, 7, 8 are reserved and `users-bs` is set to 3, the base station is SRN 3, while SRNs 5, 7 and 8 are the 1st, 2nd and 3rd users, respectively
- `tenant-number`: Number of network slicing tenants. By default, a maximum of 10 tenants is supported. In case more than 10 tenants are needed, also modify `MAX_SLICING_TENANTS` in `radio_code/srsLTE/srsenb/hdr/global_variables.h`
- `users-bs`: Maximum number of users per base station. This parameter is used to "elect" the base stations in the network. E.g., if `users-bs` is set to 3 and there are 8 nodes in the reservation, nodes 1 and 5 are elected as base station and nodes 2, 3, 4, 6, 7, 8 are UEs
- `write-config-parameters`: If enabled, writes configuration parameters on file (e.g., scheduling/slicing policies). Done at the base station-side

### radio_code

The `radio_code` directory contains a modified version of <a href="https://github.com/srsran/srsRAN" target="_blank">srsLTE</a> (now srsRAN) that implements the 5G-oriented functionalities enabled by SCOPE, configuration files and support applications.

- `srsLTE`: This is a modified version of srsLTE with the 5G-oriented functionalities enabled by SCOPE (see Section 3 of [[1]](#1))
- `scope_config`: Configuration files used by SCOPE base station. A template version of these files is stored in `../srsLTE/config_files/scope_config`
    - `scope_cfg.txt`: Global configuration file to enable/disable SCOPE functionalities. All parameters are disabled if not found in the file. Loaded parameters:
        - `colosseum_testbed`: Enables Colosseum-specific configuration of radio parameters
        - `force_dl_modulation`/`force_ul_modulation`: Enables forcing of downlink/uplink modulation for selected users. The actual modulation can be specified in `slicing/ue_imsi_modulation_dl.txt`/`ue_imsi_modulation_ul.txt`
        - `global_scheduling_policy`: Specifies scheduling policy for the whole network.<sup>[2](#footnote2)</sup> Available choices are:
            - `0`: Round-robin scheduling policy
            - `1`: Waterfilling scheduling policy
            - `2`: Proportionally fair scheduling policy
        - `network_slicing_enabled`: Enables network slicing loading slicing-related configuration files in the `slicing` directory
    - `remove_experiment_data.sh`: Removes collected data from old runs
    - `config`: This directory gets populated at run time with user-related parameters (e.g., downlink power scaling factor)
    - `metrics/csv`: CSV files on user performance are automatically logged in this directory at run time
    - `slicing`: Contains slicing- and user- related configuration files:
        - `slice_allocation_mask_tenant_*.txt`: 25-bit RBG allocation mask for tenant. This is a binary mask in which a 1 denotes that the RBG is permitted for use in the specific slice, a 0 that it is not permitted. The number of PRBs contained in a RBG, and the number of RBGs read depend on the configuration of the base station, i.e., by the total number of PRBs used (see <a href="https://www.etsi.org/deliver/etsi_ts/136200_136299/136213/08.08.00_60/ts_136213v080800p.pdf#%5B%7B%22num%22%3A123%2C%22gen%22%3A0%7D%2C%7B%22name%22%3A%22FitH%22%7D%2C264%5D" target="_blank">3GPP TS 36.213, Table 7.1.6.1-1</a>)
        - `slice_scheduling_policy.txt`: Specifies the scheduling policy to use for each network slice. Available choices are:
            - `0`: Round-robin scheduling policy
            - `1`: Waterfilling scheduling policy
            - `2`: Proportionally fair scheduling policy
        - `ue_imsi_modulation_dl.txt`/`ue_imsi_modulation_ul.txt`: Configuration file to force downlink/uplink modulation for specific users<sup>[3](#footnote3)</sup>
        - `ue_imsi_slice.txt`: Slice-users associations<sup>[3](#footnote3)</sup>
- `srslte_config`: Configuration files for cellular applications used by srsLTE, adapted for use in Colosseum. Generic configuration templates for use in different environments are stored in `../srsLTE/config_files/general_config`

## SCOPE Cellular Scenarios for Colosseum Network Emulator

These are a set of RF cellular scenarios to use with the Colosseum wireless network emulator (see Section 4 of [[1]](#1)). Scenarios have been designed in three different urban setups: (i) Rome, Italy; (ii) Boston, MA, US, and (iii) Salt Lake City, UT, US (POWDER scenario).
For the Rome and Boston scenarios, the locations of the base station reflect real cell tower deployments taken from the <a href="https://opencellid.org" target="_blank">OpenCelliD</a> database.
For the POWDER scenario, locations mirror the coordinates of the rooftop base stations deployed in the <a href="https://www.powderwireless.net/area" target="_blank">POWDER testbed</a> in Salt Lake City.

Different versions of the above scenarios have been created for different number of base stations and users, different distance from the base stations users are deployed, and different mobility speed of the users.
The available distances between users and base stations are: `close`, users are deployed within 20 m from the base stations, `medium`, within 50 m, and `far`, within 100 m. The available user mobility speeds are: `static`, users do not move for the whole duration of the scenario, `moderate`, users move at 3 m/s, and `fast`, users move at 5 m/s.

| Terrain | UE-BS Distance | UE Mobility | BS&#160;# | UEs/BS | Node&#160;# | Duration [s] | Scenario ID | Available&#160;on Colosseum |
|:--------|:---------------|:------------|:---------:|:------:|:-----------:|:------------:|:-----------:|:---------------------------:|
| Rome    | close          | static      | 10        | 4      | 50          | 600          | 1017        | :heavy_check_mark:          |
| Rome    | close          | moderate    | 10        | 4      | 50          | 600          | 1018        | :heavy_check_mark:          |
| Rome    | close          | fast        | 10        | 4      | 50          | 600          | 1028        | :x:                         |
| Rome    | medium         | static      | 10        | 4      | 50          | 600          | 1035        | :heavy_check_mark:          |
| Rome    | far            | static      | 10        | 4      | 50          | 600          | 1019        | :heavy_check_mark:          |
| Boston  | close          | static      | 10        | 4      | 50          | 600          | 1031        | :heavy_check_mark:          |
| Boston  | close          | moderate    | 10        | 4      | 50          | 600          | 1033        | :heavy_check_mark:          |
| Boston  | close          | fast        | 10        | 4      | 50          | 600          | 1034        | :x:                         |
| Boston  | medium         | static      | 10        | 4      | 50          | 600          | 1036        | :heavy_check_mark:          |
| Boston  | far            | static      | 10        | 4      | 50          | 600          | 1024        | :heavy_check_mark:          |
| POWDER  | close          | static      | 8         | 4      | 40          | 600          | 1025        | :heavy_check_mark:          |
| POWDER  | close          | moderate    | 8         | 4      | 40          | 600          | 1026        | :heavy_check_mark:          |
| POWDER  | close          | fast        | 8         | 4      | 40          | 600          | 1030        | :x:                         |
| POWDER  | medium         | static      | 8         | 4      | 40          | 600          | 1041        | :heavy_check_mark:          |
| POWDER  | far            | static      | 8         | 4      | 40          | 600          | 1027        | :heavy_check_mark:          |

Due to space limitations, only a selection of these scenarios is currently available on Colosseum. The remaining scenarios can be built and installed upon request.

## References
<a id="1">[1]</a> 
L. Bonati, S. D'Oro, S. Basagni, and T. Melodia,
<i>"SCOPE: An Open and Softwarized Prototyping Platform for NextG Systems,"</i>
in Proceedings of ACM MobiSys, June 2021.
<a href="https://ece.northeastern.edu/wineslab/papers/bonati2021scope.pdf" target="_blank">[pdf]</a>
<a href="https://ece.northeastern.edu/wineslab/wines_bibtex/bonati2021scope.txt" target="_blank">[bibtex]</a>

---

<a id="footnote1">1</a>. These parameters are periodically reloaded from file. The frequency at which they are reloaded can be modified through the variable `line_change_frequency_ms` in `../radio_code/srsLTE/srsenb/src/stack/mac/scheduler_metric.cc`, function `dl_metric_rr::sched_users`. \
<a id="footnote2">2</a>. This parameter is ignored if network slicing is enabled (`network_slicing_enabled`). In such case, the scheduling policy can be specified through the `slicing/slice_scheduling_policy.txt` configuration file. \
<a id="footnote3">3</a>. When adding a new user to the core network database, the relative IMSI needs to be added to this configuration files as well.
