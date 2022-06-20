#!/bin/bash

for P in 1 2 4 8 16 24
do
	echo $P " " $1 " " $2 >> time_capio_writes.txt
	echo $P " " $1 " " $2 >> time_capio_reads.txt
	./test_capio_batch_pipeline.sh $P $1 $2 "$3"
done
