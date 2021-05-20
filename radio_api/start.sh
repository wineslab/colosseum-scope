#!/usr/bin/env bash
# start.sh - This script is called by Colosseum to tell the radio the job is starting.
# No input is accepted.
# STDOUT and STDERR may be logged, but the exit status is always checked.
# The script should return 0 to signify successful execution.

# send "s" to /tmp/mypipe (a named pipe that the incumbent is monitoring)

echo "[`date`] Ran start.sh" >> /logs/run.log

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
SCOPE_CONFIG=$(cat ${SCRIPT_DIR}/constants.py | grep "SCOPE_CONFIG" | awk -F"'" '{print $2}')

echo "[`date`] SCRIPT_DIR ${SCRIPT_DIR}" >> /logs/run.log
echo "[`date`] SCOPE_CONFIG ${SCOPE_CONFIG}" >> /logs/run.log

echo "[`date`] Removing old srsLTE data" >> /logs/run.log
${SCOPE_CONFIG}remove_experiment_data.sh

echo "[`date`] Starting srsLTE applications" >> /logs/run.log
python3 ${SCRIPT_DIR}/scope_start.py --config-file radio.conf

exit 0
