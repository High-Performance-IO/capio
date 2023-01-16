In this example we have a workflow composed by two applications.
The first application writes 6 files and the second application reads these files.
The streaming semantics for the file file0.dat is on_close.
The streaming semantics for the file file1.dat is no_update.
The streaming semantics for the file file2.dat is not specified. This means that the default on_termination semantics is used.
The streaming semantics for the file file3.dat is on_termination.
The streaming semantics for the file file4.dat is no_update. 


If the writer and the reader write and read the files in order (file0.dat, file1.dat, etc...) the file2.dat will interrupt the streaming communication because the semantics is on_termination and this means that the reader can start reading that file only after the writer is terminated.
