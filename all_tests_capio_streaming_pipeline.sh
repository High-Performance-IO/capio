#!/bin/bash

for P in 1 2 4 8 16 24
do
echo $P " " $1 " " $2 >> time_streaming_pipeline_capio.txt
echo $P " " $1 " " $2 >> time_capio_writes.txt
echo $P " " $1 " " $2 >> time_capio_reads.txt
	echo test P $P
	{ time ./test_capio_streaming_pipeline.sh $P $1 $2 "$3" ; } 2> tmp.txt 
	grep real tmp.txt >> time_streaming_pipeline_capio.txt
	sleep 10
	#./clean.sh
	#ssh -p 2222 ibnode19 'rm /dev/shm/*'
done
