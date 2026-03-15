#!/bin/bash

OUTPUT_PATH="${1}"
PROVIDED_COMPILER="${2}"

if [ -z "${OUTPUT_PATH}" ]; then
  echo "Usage: $(basename "${BASH_SOURCE[0]}") OUTPUT_PATH [COMPILER_PATH]"
  exit 1
fi

CC="${PROVIDED_COMPILER:-$(command -v gcc || command -v clang)}"

if [ -z "$CC" ]; then
    echo "Error: No compiler found." >&2
    exit 1
fi

OUTPUT_DIRECTORY=$(dirname $OUTPUT_PATH)

echo "Generating syscall table using $CC..."
echo "Output directory: $OUTPUT_DIRECTORY"
echo "Output path: $OUTPUT_PATH"

mkdir -p "$OUTPUT_DIRECTORY"

# Create a temporary C file to resolve macro values
TEMP_C=$(mktemp -t syscall_gen.XXXXXX.c)
echo "#include <asm/unistd.h>" > "$TEMP_C"
$CC -E -dM "$TEMP_C" | grep "#define __NR_" | awk '{print $2}' | sort | uniq | while read -r line; do
    echo "VAL_$line=$line" >> "$TEMP_C"
done

# Run preprocessor on our temp file to get the resolved integers
RESOLVED_DEFS=$($CC -E -P "$TEMP_C" | grep "VAL___NR_")
rm "$TEMP_C"

{
    echo "#include <string>"
    echo ""
    echo "inline std::string sys_num_to_string(int sysnum) {"
    echo "    switch (sysnum) {"

    echo "$RESOLVED_DEFS" | while IFS='=' read -r label value; do
        name=${label#VAL___NR_}
        if [[ "$name" == "syscalls" || "$name" == "ia32_syscalls" || "$name" == "x32_syscalls" ]]; then
            continue
        fi
        if [[ "$value" =~ ^[0-9]+$ ]]; then
            echo "        case $value: return \"$name\";"
        fi
    done | sort -n -u -k2

    echo "        default: return \"Unknown (\" + std::to_string(sysnum) + \")\";"
    echo "    }"
    echo "}"
} > "${OUTPUT_PATH}"

echo "Done! Generated $(grep "case" "$OUTPUT_PATH" | wc -l) syscalls."