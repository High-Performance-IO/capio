#!/bin/bash

for P in 1
do
	{ time ./test_capio_streaming_pipeline.sh $P $1 $2 $3 ; } 2> >(grep real >> time_streaming_pipeline_capio.txt)
	./clean.sh
done
