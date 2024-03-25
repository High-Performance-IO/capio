#include <mapreduce.h>


static int writedata(char *dataptr, size_t datalen, float percent,
		     char *destdir, ssize_t dstart, ssize_t dfiles) {
  int error=0;
  FILE **fp = calloc(sizeof(FILE*),dfiles);
  if (!fp) {
    perror("malloc");
    return -1;
  }  
  char filepath[strlen(destdir)+maxfilename];

  // opening (truncating) all files
  for(int j=0, i=0+dstart;i<(dfiles + dstart);++i, ++j) {
    sprintf(filepath, fmtout, destdir, i); 
    fp[j] = fopen(filepath, "w");
    if (!fp[j]) {
      perror("fopen");
      fprintf(stderr, "cannot create (open) the file %s\n", filepath);
      error = -1;
    }    
  }

  if (!error) {
    size_t nbytes = datalen*percent;
    size_t cnt = 0;
    while(nbytes>0) {
      size_t chunk = (nbytes>REDUCE_CHUNK)?REDUCE_CHUNK:nbytes;
      if (fwrite(dataptr, 1, chunk, fp[cnt]) != chunk) {
	perror("fwrite");
	error = -1;
	break;
      }
      cnt = (cnt + 1) % dfiles;
      nbytes -= chunk;
    }
  }
  // closing all files
  for(int i=0;i<dfiles;++i) {
    if (fp[i]) fclose(fp[i]);
  }
  free(fp);
  return error;
}


int main(int argc, char *argv[]) {
  (void)phrases;

  struct timeval before, after;
  gettimeofday(&before, NULL);

  if (argc != 8) {
    fprintf(stderr, "use: %s sourcedir #sstart #sfiles destdir #dstart #dfiles percent\n", argv[0]);
    return -1;
  }
  struct stat statbuf;
  if (stat(argv[1], &statbuf)==-1) {
    perror("stat source");
    fprintf(stderr, "Does the directory %s exit?\n", argv[1]);
    return -1;
  }
  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr, "%s is not a directory!\n", argv[1]);
    return -1;
  }
  char *sourcedirname = argv[1];
  ssize_t sstart  = strtol(argv[2], NULL, 10);
  if (sstart<0) {
    fprintf(stderr, "Invalid #sstart (%ld)\n", sstart);
    return -1;
  }
  ssize_t sfiles  = strtol(argv[3], NULL, 10);
  if (sfiles==0 || sfiles<0) {
    fprintf(stderr, "Invalid #sfiles (%ld)\n", sfiles);
    return -1;
  }
  if (stat(argv[4], &statbuf)==-1) {
    perror("stat destination");
    fprintf(stderr, "Does the directory %s exit?\n", argv[4]);
    return -1;
  }
  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr, "%s is not a directory!\n", argv[4]);
    return -1;
  }
  char *destdirname = argv[4];
  ssize_t dstart  = strtol(argv[5], NULL, 10);
  if (dstart<0) {
    fprintf(stderr, "Invalid #dstart (%ld)\n", dstart);
    return -1;
  }
  ssize_t dfiles  = strtol(argv[6], NULL, 10);
  if (dfiles==0 || dfiles<0) {
    fprintf(stderr, "Invalid #dfiles (%ld)\n", dfiles);
    return -1;
  }
  float percent = strtof(argv[7], NULL);
  if (percent > 1 || percent<=0) {
    fprintf(stderr, "Invalid percent (%f)\n", percent);
    return -1;
  }

  char *dataptr=NULL; 
  size_t datalen=0;
  size_t datacapacity=0;
  char filepath[strlen(sourcedirname) + maxfilename];
  // concatenating all files in memory (dataptr)
  for(int i=0+sstart;i<(sfiles + sstart);++i) {
    sprintf(filepath, fmtin, sourcedirname, i); 
    FILE *fp= fopen(filepath, "r");
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
  
  int r = writedata(dataptr, datalen, percent, destdirname, dstart, dfiles);
  free(dataptr);


  gettimeofday(&after, NULL);
  double elapsed_time = diffmsec(after, before);
  fprintf(stdout, "MAPREDUCE: elapsed time (ms) : %g\n", elapsed_time);

  
  return r;
  
}
