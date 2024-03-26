#!/bin/bash

#run with CAPIO_DIR=blablah CAPIO_BUILD=blahblahblah ./run.sh 23 23 1

rm -rf capio_logs server.log files_location.txt 
rm -rf /dev/shm/*
rm -rf $CAPIO_DIR
mkdir -p $CAPIO_DIR
datadir="$CAPIO_DIR/data"
resultdir="$CAPIO_DIR/result"
lines=100 #number of lines per file
mapreducers=$1
files=$(($lines / $1))   # number of per mapper/reducer files
percent=$2
CAPIO_LD=$CAPIO_BUILD/src/posix/libcapio_posix.so
export CAPIO_LOG_LEVEL=-1


$CAPIO_BUILD/src/server/capio_server --no-config > server.log &
SERVER_PID=$!
echo "Capio server warmup of 5 seconds..."
sleep 5 #wait for server startup


echo datadir=$datadir
echo resultdir=$resultdir
echo lines_per_file=$lines
echo mapreducers=$2
echo files_per_map_reducer=$files   # number of per mapper/reducer files
echo percent=$4
echo CAPIO_LD=$CAPIO_LD


LD_PRELOAD=$CAPIO_LD mkdir "$datadir"
if [ $? -ne 0 ]; then
    echo "LD_PRELOAD=$CAPIO_LD mkdir $datadir ===> failed exit code $?"
    kill -9 $SERVER_PID
    exit -1
fi

LD_PRELOAD=$CAPIO_LD mkdir $resultdir
if [ $? -ne 0 ]; then
    echo "LD_PRELOAD=$CAPIO_LD mkdir $resultdir ===> failed exit code $?"
    kill -9 $SERVER_PID
    exit -1
fi

totalmapfiles=$(($mapreducers * $files))

echo "executing: ./split $lines $totalmapfiles $datadir"
LD_PRELOAD=$CAPIO_LD ./split $lines $totalmapfiles $datadir
if [ $? -ne 0 ]; then
    echo "split failed exit code $?"
    kill -9 $SERVER_PID
    exit -1
fi
next=0
MAPREDUCE_PIDS=""
for((i=0;i<$mapreducers;++i)); do
    echo "executing: ./mapreduce $datadir $next $files $datadir $next $files $percent &"
    LD_PRELOAD=$CAPIO_LD ./mapreduce $datadir $next $files $datadir $next $files $percent &
    next=$(($next+$files))
    MAPREDUCE_PIDS="$MAPREDUCE_PIDS $!"
done
echo "waiting..."

# wait for all pids
for job in $MAPREDUCE_PIDS; do
    wait $job 2> /dev/null
    if [ $? -ne 0 ]; then
	echo "mapreduce ($i) failed exit code $?"
    fi
done

echo "executing: ./merge $totalmapfiles $datadir $resultdir"
LD_PRELOAD=$CAPIO_LD ./merge 1 $datadir $resultdir
if [ $? -ne 0 ]; then
    echo "merge failed exit code $?"
    kill -9 $SERVER_PID
    exit -1
fi

kill -9 $SERVER_PID

exit 0
