#!/usr/bin/env bash
# status.sh - This script is called by the Colosseum to check on the radio state.
# No input is accepted.
# Output should be given by way of STDOUT as a serialized JSON dictionary.
# The dictionary must contain a STATUS key with a state string as a value.
# The dictionary may contain an INFO key with more detailed messages for the team.
# STDERR may be logged, but the exit status is always checked.
# The script should return 0 to signify successful execution.

# ---Example Usage---
#check if there is an input argument for error exit example
if [ $# -ne 0 ]
then exit 64 #exit with an error
fi

MESSAGE=""

# check to see if our state file even exists
if [ -e /tmp/LTE_STATE ]; then
    STATE=`cat /tmp/LTE_STATE`

    if [ -z "$STATE" ]; then
        STATE="ERROR"
        MESSAGE="LTE_STATE is blank"
    fi
else
    STATE="ERROR"
    MESSAGE="LTE_STATE is missing"
fi

# put the state in a serialized dictionary
OUTPUT="{\"STATUS\":\"$STATE\",\"INFO\":\"$MESSAGE\"}"

#print to STDOUT
echo $OUTPUT

#exit good
exit 0
