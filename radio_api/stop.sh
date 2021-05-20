#!/usr/bin/env bash
# stop.sh - This script is called by Colosseum to tell the radio the job is ending.
# No input is accepted.
# STDOUT and STDERR may be logged, but the exit status is always checked.
# The script should return 0 to signify successful execution.

# set LTE transceiver state to STOPPING
echo "STOPPING" > /tmp/LTE_STATE

# stop srsLTE
echo "[`date`] Ran stop.sh" >> /logs/run.log
pkill -9 srsue
pkill -9 srsenb
pkill -9 srsepc
pkill -9 tcpdump
echo "[`date`] Stopped srsLTE" >> /logs/run.log

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
SCOPE_CONFIG=$(cat ${SCRIPT_DIR}/constants.py | grep "SCOPE_CONFIG" | awk -F"'" '{print $2}')

echo "[`date`] SCRIPT_DIR ${SCRIPT_DIR}" >> /logs/run.log
echo "[`date`] SCOPE_CONFIG ${SCOPE_CONFIG}" >> /logs/run.log

# copy radio.conf and colosseum_config.ini to /logs/
cp ${SCRIPT_DIR}/radio.conf /logs/
cp ${SCRIPT_DIR}/colosseum_config.ini /logs/

# copy srsLTE metrics and log files
cp /tmp/epc.log /logs/
cp /tmp/enb.log /logs/
cp /tmp/enb_metrics.csv /logs/

cp /tmp/ue.log /logs/
cp /tmp/ue_metrics.csv /logs/

# copy scope_config directory
cp -R ${SCOPE_CONFIG} /logs/

# copy journalctl to /logs/
journalctl --since="1 hours ago" > /logs/journalctl.log

# set LTE transceiver state to FINISHED
echo "FINISHED" > /tmp/LTE_STATE

exit 0
