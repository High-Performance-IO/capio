#!/bin/bash

#SBATCH --exclusive
#SBATCH --job-name=100genome
#SBATCH --error=myJob%j.err            # standard error file
#SBATCH --output=myJob%j.out           # standard output file


echo Number of nodes $SLURM_NNODES
read -d ' ' -a list <<< "$(scontrol show hostnames $SLURM_NODELIST)"

if [ $# -ne 1 ]
then
	echo "Usage: $0 CAPIO_DIR"
	exit 1
fi

capio_dir=$1
file_size=$((1024*1024))
n_lines=250000
n_individuals=$((SLURM_NNODES - 5))
lines_per_individual=$((n_lines / n_individuals))

srun --exact --export=ALL,CAPIO_FILE_INIT_SIZE="$file_size" -N $SLURM_NNODES -n $SLURM_NNODES --ntasks-per-node=1 $capio_dir/src/capio_server server.log &
SERVER_PID=$!

srun -N 1 -n 1 -w ${list[0]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="download" cp -r ../../data . 2>  time_download_$SLURM_NNODES.txt
echo "cp terminated"

srun -N 1 -n 1 -w ${list[1]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="sifting" ./sifting data/20130502/sifting/ALL.chr1.phase3_shapeit2_mvncall_integrated_v5.20130502.sites.annotation.vcf 1
SIFTING_PID=$!

i=1
individuals_pids=()
dirs_merge=()
j=2
echo n_individuals $n_individuals
while [ "$i" -le "$n_individuals" ]
do
	first_line=$(((i - 1) * lines_per_individual + 1))
	last_line=$((first_line + lines_per_individual - 1))
	srun -N 1 -n 1 -w ${list[$j]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="individuals" ./individuals data/20130502/ALL.chr1.250000.vcf 1 $first_line $last_line $n_lines 2> individuals.log &
	individuals_pids+=($!)
	dir="chr1n-$first_line-$last_line"
	echo first line $first_line last line $last_line dir $dir
	dirs_merge+=($dir)
	((++i))
	((++j))
done
wait  ${individuals_pids[*]}
echo individuals terminated
echo merging ${dirs_merge[*]}

#ssh ${list[1]} "cd /g100_scratch/userexternal/icolonne/capio_tests/1000-genome/cpp/build && LD_PRELOAD=~/capio_tests/capio/build/libcapio_posix.so cp -r chr1n-1-2000 ../chr1n-1-2000_capio > cp_individuals_out.txt 2> cp_individuals_out.log"

node_index=$((2 + n_individuals))
echo node_index $node_index
srun -N 1 -n 1 -w ${list[$node_index]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="individuals_merge" ./individuals_merge 1 ${dirs_merge[*]} 2> individuals_merge.log
echo "individuals_merge terminated"

echo "sifting terminated"
#ssh ${list[2]} "cd /g100_scratch/userexternal/icolonne/capio_tests/1000-genome/cpp/build && LD_PRELOAD=~/capio_tests/capio/build/libcapio_posix.so cp -r chr1n ../chr1n_capio > cp_data_out.txt 2> cp_data_out.log"


#ssh ${list[3]} "cd /g100_scratch/userexternal/icolonne/capio_tests/1000-genome/cpp/build && LD_PRELOAD=~/capio_tests/capio/build/libcapio_posix.so cp sifted.SIFT.chr1.txt ../sifted.SIFT.chr1.txt_capio > cp_data_out.txt 2> cp_data_out.log"

echo "sifting copy terminated"

((++node_index))
echo node_index $node_index
srun -N 1 -n 1 -w ${list[$node_index]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="mutation_overlap" ./mutation_overlap --c 1 --pop ALL 2> mutation_overlap.log &
MUTATION_PID=$!

echo "mutation overlap terminated"

#ssh ${list[$node_index]} "cd /g100_scratch/userexternal/icolonne/capio_tests/1000-genome/cpp/build && LD_PRELOAD=~/capio_tests/capio/build/libcapio_posix.so cp chr1-ALL.tar.gz ../chr1-ALL_capio.tar.gz > cp_data_out.txt 2> cp_data_out.log"

echo "mutation overlap copy terminated"

((++node_index))
echo node_index $node_index
srun -N 1 -n 1 -w ${list[$node_index]} --exact --export=ALL,LD_PRELOAD="$capio_dir/libcapio_posix.so",CAPIO_APP_NAME="frequency" ./frequency -c 1 -pop ALL

echo "frequency terminated"

wait $MUTATION_PID

#ssh ${list[$node_index]} "cd /g100_scratch/userexternal/icolonne/capio_tests/1000-genome/cpp/build && LD_PRELOAD=~/capio_tests/capio/build/libcapio_posix.so cp chr1-ALL-freq.tar.gz ../chr1-ALL-freq_capio.tar.gz > cp_data_out.txt 2> cp_data_out.log"


kill $SERVER_PID
sleep 15
