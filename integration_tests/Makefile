CC=gcc
CFLAGS=-Wall -I ./ -O3
.PHONY=clean cleanall
TARGET= split mapreduce merge

%:%.c
	$(CC) $(CFLAGS) $< -o $@

all: $(TARGET)

split:mapreduce.h
mapreduce:mapreduce.h
merge:mapreduce.h

clean:
	\rm -f $(TARGET)
	\rm -rf capio_logs file_location.txt server.log
cleanall:clean
	\rm -f *~ infile_?????.dat outfile_?????.dat result.dat


