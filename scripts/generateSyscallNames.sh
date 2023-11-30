#!/bin/bash
SCRIPT_DIRECTORY="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ARCH=$(uname -m)
input=$(echo /usr/include/"$ARCH"-linux-gnu/asm/unistd_$(echo "$ARCH" | rev | cut -c 1-2 | rev).h)
DESTINATION="${SCRIPT_DIRECTORY}/../src/common/capio/syscallnames.h"

echo "Parsing $input"
DATA="auto sys_num_to_string(int sysnum) {
  switch (sysnum) {" > $DESTINATION

while IFS= read -r line
do
  if [[ "$(echo "$line" | grep '#define __NR_')" != "" ]]
  then
    SYSCALL=$(echo "$line" | cut -d ' ' -f 2)
    SYSCALL=${SYSCALL#"__NR_"}
    SYSCALLNO=$(echo "$line" | cut -d ' ' -f 3)
    #echo "Adding entry for sysycall: $SYSCALL with value $SYSCALLNO"
    DATA="$DATA
        case $SYSCALLNO:
        return \"$SYSCALL\";" >> $DESTINATION
  fi
done < "$input"

DATA="$DATA
    default:
        return \"Unknown\";
    };
};"

echo $DATA>$DESTINATION
