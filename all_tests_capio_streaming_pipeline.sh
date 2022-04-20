#!/bin/bash

for P in 1 2 4 8 16 24
do
	{ time ./test_capio_streaming_pipeline.sh $P $1 $2 "$3" ; } 2> tmp.txt 
	grep real tmp.txt >> time_streaming_pipeline_capio.txt
	./clean.sh
	sleep 3
done
