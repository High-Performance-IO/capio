#!/bin/bash
perl -nE '
BEGIN { say
"auto sys_num_to_string(int sysnum) {
    switch (sysnum) {" }
if (/__NR_(\w+) (\d+)/) { say qq/    case $2: \n        return "$1";/ }
END { say "    default:\n        return \"ukn\";\n    };\n};" }' /usr/include/$(uname -m)-linux-gnu/asm/unistd_64.h > src/common/capio/syscallnames.h