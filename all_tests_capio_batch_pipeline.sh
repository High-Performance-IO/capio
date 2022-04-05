#!/bin/bash

for P in 1 2 4 8 16 24
do
	./test_capio_batch_pipeline.sh $P $1 $2 "$3"
done
