#!/bin/bash

if [ -z "${1}" ]; then
  echo "Usage: $(basename "${BASH_SOURCE[0]}") OUTPUT_PATH"
  exit
fi

ARCH=$(uname -m | rev | cut -c 1-2 | rev)
INPUT=$(find /usr/include -name "unistd_${ARCH}.h")
DESTINATION="${1}"

# Generate default destination file
mkdir -p "$(dirname "${DESTINATION}")"
echo 'auto sys_num_to_string(int sysnum) { return "Table not created"; }' > "${DESTINATION}"

# Parse syscall names
echo "Parsing ${INPUT}"
DATA="auto sys_num_to_string(int sysnum) {
  switch (sysnum) {" > "${DESTINATION}"

while IFS= read -r line
do
  if [[ "$(echo "$line" | grep '#define __NR_')" != "" ]]
  then
    SYSCALL=$(echo "$line" | cut -d ' ' -f 2)
    SYSCALL=${SYSCALL#"__NR_"}
    SYSCALLNO=$(echo "$line" | cut -d ' ' -f 3)
    DATA="$DATA
        case $SYSCALLNO:
        return \"$SYSCALL\";" >> "${DESTINATION}"
  fi
done < "${INPUT}"

# Add default branch
DATA="$DATA
    default:
        return \"Unknown\";
    };
};"

# Print content to the destination file
echo "${DATA}" > "${DESTINATION}"
