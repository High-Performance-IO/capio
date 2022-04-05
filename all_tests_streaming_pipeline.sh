#!/bin/bash

for P in 1 2 4 8 12 16 24
do
	{ time ./test_streaming_pipeline.sh $P $1 $2 "$3" ; } 2> >(grep real >> time_streaming_pipeline.txt)
	sleep 1
	./clean.sh
done
