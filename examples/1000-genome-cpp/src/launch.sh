#!/bin/bash

./download 1
./individuals data/20130502/ALL.chr1.250000.vcf 1 1 2000 10000
./individuals_merge 1 chr1n-1-2000.tar.gz
./sifting data/20130502/sifting/ALL.chr1.phase3_shapeit2_mvncall_integrated_v5.20130502.sites.annotation.vcf
./mutation_overlap --c 1 --pop ALL 2> out.log > out.txt
./frequency -c 1 -pop ALL
