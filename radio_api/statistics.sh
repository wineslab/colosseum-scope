#!/usr/bin/env bash
# v0.5.0
# statistics.sh - This script is called by the Colosseum to check on the radio performance. 
# No input is accepted.
# Output should be given by way of STDOUT as a serialized JSON dictionary.
# The dictionary structure is still TBD.
# STDERR may be logged, but the exit status is always checked.
# The script should return 0 to signify successful execution.

# EXAMPLE FROM status.sh FOR OUTPUT FORMAT
# ---Example Usage---
#check if there is an input argument for error exit example
if [ $# -ne 0 ] 
then exit 64 #exit with an error
fi

#pick a state to return as an example
STATE_NUM=$[ RANDOM % 7 ]

#assign state string based on number
case "$STATE_NUM" in
     0)
          STATE="OFF"
          ;;
     1)
          STATE="BOOTING"
          ;;
     2)
          STATE="READY"
          ;;
     3)
          STATE="ACTIVE"
          ;;
     4)
          STATE="STOPPING"
          ;;
     5)
          STATE="FINISHED"
          ;;
     6)
          STATE="ERROR"
          ;;
     *)
          #something went wrong
          exit 64 #exit with an error
          ;;
esac

#put the state in a serialized dictionary
OUTPUT="{\"STATUS\":\"$STATE\",\"INFO\":\"\"}"

#print to STDOUT
echo $OUTPUT

#exit good
exit 0

# While this script can directly issue commands to the radio, 
# another use case may be to keep a radio control daemon running 
# and have this script interface with the daemon.
# The exact way this script queries radio status is up
# to individual teams so long as the required outputs are returned.
