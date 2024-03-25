#include <mapreduce.h>




int main(int argc, char *argv[]) {
  (void)fmtin;
  (void)phrases;

  struct timeval before, after;
  gettimeofday(&before, NULL);


  if (argc != 4) {
    fprintf(stderr, "use: %s #files sourcedir destdir\n", argv[0]);
    return -1;
  }
  ssize_t nfiles  = strtol(argv[1], NULL, 10);
  if (nfiles==0 || nfiles<0) {
    fprintf(stderr, "Invalid #files (%ld)\n", nfiles);
    return -1;
  }
  struct stat statbuf;
  if (stat(argv[2], &statbuf)==-1) {
    perror("stat");
    fprintf(stderr, "Does the directory %s exit?\n", argv[2]);
    return -1;
  }
  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr, "%s is not a directory!\n", argv[2]);
    return -1;
  }
  if (stat(argv[3], &statbuf)==-1) {
    perror("stat");
    fprintf(stderr, "Does the directory %s exit?\n", argv[3]);
    return -1;
  }
  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr, "%s is not a directory!\n", argv[3]);
    return -1;
  }

  char *sourcedir = argv[2];
  char *destdir   = argv[3];
  char *dataptr=NULL; 
  size_t datalen=0;
  size_t datacapacity=0;
  char filepath[strlen(sourcedir)+maxfilename];
  for(int i=0;i<nfiles;++i) {
    sprintf(filepath, fmtout, sourcedir, i); 
    printf("%s\n",filepath);
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
      perror("fopen");
      fprintf(stderr, "cannot open the file %s\n", filepath);
      return -1;
    }
    char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);    
    if (ptr==NULL) {
      free(dataptr);
      return -1;
    }
    dataptr = ptr;
    fclose(fp);
  }
  int error=0;
  char resultpath[strlen(destdir)+strlen("/result.dat")];
  sprintf(resultpath, "%s/result.dat", destdir);
  FILE *fp = fopen(resultpath, "w");
  if (!fp) {
    perror("fopen");
    fprintf(stderr, "cannot creat %s\n", resultpath);
    error=-1;
  } else {
    if (fwrite(dataptr, 1, datalen, fp) != datalen) {
      perror("fwrite");
      error = -1;
    }
  }
  free(dataptr);

  gettimeofday(&after, NULL);
  double elapsed_time = diffmsec(after, before);
  fprintf(stdout, "MERGE: elapsed time (ms) : %g\n", elapsed_time);

  
  return error;

}
