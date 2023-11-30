#!/bin/bash
ARCH=$(uname -m)
input=$(echo /usr/include/"$ARCH"-linux-gnu/asm/unistd_$(echo "$ARCH" | rev | cut -c 1-2 | rev).h)
DESTINATION=src/common/capio/syscallnames.h

echo "Parsing $input"
echo "auto sys_num_to_string(int sysnum) {
  switch (sysnum) {" > $DESTINATION

while IFS= read -r line
do
  if [[ "$(echo "$line" | grep '#define __NR_')" != "" ]]
  then
    SYSCALL=$(echo "$line" | cut -d ' ' -f 2)
    SYSCALL=${SYSCALL#"__NR_"}
    SYSCALLNO=$(echo "$line" | cut -d ' ' -f 3)
    #echo "Adding entry for sysycall: $SYSCALL with value $SYSCALLNO"
    echo "    case $SYSCALLNO:
        return \"$SYSCALL\";" >> $DESTINATION
  fi
done < "$input"

echo "    default:
        return \"Unknown\";
    };
};" >> $DESTINATION
