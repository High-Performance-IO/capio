#!/bin/bash

../../build/experiments/queues/write_file 100 100 && ../../build/experiments/queues/read_file 100 100 > output_read_file.txt
