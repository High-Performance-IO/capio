#!/bin/sh

ITERATIONS=$1
LIBSYSCALL_INTERCEPT=$2
LIBCAPIO_POSIX=$3
REPETITIONS=${3:-100}
WARMUP=${4:-10}

git clone https://github.com/intel/lmbench
sed -i 's/LDLIBS=-lm/LDLIBS="-lm -ltirpc"/g' lmbench/scripts/build
CPPFLAGS="-I/usr/include/tirpc" make -C lmbench
LAT_SYSCALL="./lmbench/bin/$(uname -m)-linux-gnu/lat_syscall "
LAT_PROC="./lmbench/bin/$(uname -m)-linux-gnu/lat_proc"

echo
echo "Running lat_syscall and lat_proc from LMBench Benchmark Suite"
echo "============================================================="
echo
echo "EXECUTING $ITERATIONS ITERATIONS FOR EACH TEST"
echo

python_analisys(){
  echo "
import statistics
data = {}
cleanData = []
syscallData =[]
capioData = []
with open('$1') as source:
  source.readline()
  for line in source.readlines():
    clean, syscall, capio = line.split(',')
    cleanData.append(float(clean))
    syscallData.append(float(syscall))
    capioData.append(float(capio))

print(
  'DEFAULT : ' +str(statistics.mean(cleanData))[0:6] + ' usec(stddev: ' + str(statistics.stdev(cleanData))[0:6] + '), ' +
  'SYSCALL : ' +str(statistics.mean(syscallData))[0:6] + ' usec(stddev: ' + str(statistics.stdev(syscallData))[0:6]+ '), ' +
  'LIBCAPIO: ' +str(statistics.mean(capioData))[0:6] + ' usec(stddev: ' + str(statistics.stdev(capioData))[0:6] + ')')

  " | python3
}

rm -rf *.csv

echo "Benchmark syscall:"
I=0
echo "">warmup.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" null 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" null 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" null 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> warmup.csv

  I=$(($I+1))
done
python_analisys warmup.csv #compute average and stddev
echo



echo "Benchmark read:"
I=0
echo "">read.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" read 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" read 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" read 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> read.csv

  I=$(($I+1))
done
python_analisys read.csv #compute average and stddev
echo



echo "Benchmark write:"
I=0
echo "">write.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" write 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" write 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" write 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> write.csv

  I=$(($I+1))
done
python_analisys write.csv #compute average and stddev
echo



echo "Benchmark stat:"
I=0
echo "">stat.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" stat /var/tmp 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" stat /var/tmp 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" stat /var/tmp 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> stat.csv

  I=$(($I+1))
done
python_analisys stat.csv #compute average and stddev
echo



echo "Benchmark fstat:"
I=0
echo "">fstat.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" fstat /var/tmp 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" fstat /var/tmp 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" fstat /var/tmp 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> fstat.csv

  I=$(($I+1))
done
python_analisys fstat.csv #compute average and stddev
echo



echo "Benchmark open:"
I=0
echo "">open.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" open /var/tmp 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" open /var/tmp 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_SYSCALL -N "$REPETITIONS" -W "$WARMUP" open /var/tmp 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> open.csv

  I=$(($I+1))
done
python_analisys open.csv #compute average and stddev
echo



echo "Benchmark fork+exit:"
I=0
echo "">forkexit.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_PROC -N "$REPETITIONS" -W "$WARMUP" fork 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" fork 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" fork 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> forkexit.csv

  I=$(($I+1))
done
python_analisys forkexit.csv #compute average and stddev
echo



echo "Benchmark fork+exec:"
I=0
echo "">forkexec.csv
while [ $I -lt $ITERATIONS ]
do
  CLEAN_TIME=$($LAT_PROC -N "$REPETITIONS" -W "$WARMUP" exec 2>&1 | awk '{print $3}')
  SYSCALL_TIME=$(LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" exec 2>&1 | awk '{print $3}')
  LIBCAPIO_TIME=$(LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" exec 2>&1 | awk '{print $3}')
  echo "$CLEAN_TIME, $SYSCALL_TIME, $LIBCAPIO_TIME" >> forkexec.csv

  I=$(($I+1))
done
python_analisys forkexec.csv #compute average and stddev
echo


#echo "Benchmark fork+sh:"
#echo "int main(){return 0;}" | g++ -x c++ -o /tmp/hello - #prepare shell exec
#$LAT_PROC -N "$REPETITIONS" -W "$WARMUP" shell
#LD_PRELOAD="$LIBSYSCALL_INTERCEPT" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" shell
#LD_PRELOAD="$LIBCAPIO_POSIX" $LAT_PROC -N "$REPETITIONS" -W "$WARMUP" shell






echo "Run completed successfully"
