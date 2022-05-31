#!/bin/bash

for P in 1 2 4 8 16 24
do
	echo $P " " $1 " " $2 >> time_streaming_pipeline.txt
	echo $P " " $1 " " $2 >> time_writes.txt
	echo $P " " $1 " " $2 >> time_reads.txt
	echo test P $P
	{ time ./test_streaming_pipeline.sh $P $1 $2 "$3" ; } 2>  tmp.txt
	grep real tmp.txt >> time_streaming_pipeline.txt
	rm file_*
done
