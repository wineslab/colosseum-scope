#!/bin/bash

# get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
echo $SCRIPT_DIR

# remove created files
${SUDO} rm ${SCRIPT_DIR}/config/*.txt
${SUDO} rm ${SCRIPT_DIR}/metrics/*.txt
${SUDO} rm ${SCRIPT_DIR}/metrics/csv/*.csv
${SUDO} rm ${SCRIPT_DIR}/metrics/log/*.log

