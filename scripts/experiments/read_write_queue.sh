#!/bin/bash

../../build/experiments/queues/write_queue 100 100 > output_write_queue.txt &

../../build/experiments/queues/read_queue 100 100 > output_read_queue.txt
