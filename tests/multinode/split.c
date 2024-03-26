#include <mapreduce.h>

static char* getrandomphrase(char *buffer, size_t len) {
  static int phrases_entry = sizeof(phrases)/sizeof(phrases[0]);

  bzero(buffer,len);

  int r;
  switch((r=rand()) % 4) {
  case 0: {
    strncat(buffer, phrases[r % phrases_entry], len-1);
    break;
  }
  case 1: {
    strncat(buffer, phrases[r % phrases_entry], len-1);
    ssize_t l = len-strlen(buffer)-1;
    if (l>0) strncat(buffer, phrases[rand() % phrases_entry], l);
    break;
  }
  case 2: {
    strncat(buffer, phrases[r % phrases_entry], len-1);
    ssize_t l = len-strlen(buffer)-1;
    if (l>0) strncat(buffer, phrases[rand() % phrases_entry], l);
    l = len-strlen(buffer)-1;
    if (l>0) strncat(buffer, phrases[rand() % phrases_entry], l);
    break;
  }
  case 3: {
    strncat(buffer, phrases[r % phrases_entry], len-1);
    break;
  }
  }
  buffer[strlen(buffer)]='\n';
  
  return buffer;
}

int main(int argc, char *argv[]) {
  struct timeval before, after;
  gettimeofday(&before, NULL);
  
  (void)fmtout;
  if (argc != 4) {
    fprintf(stderr, "use: %s #lines #files destdir\n", argv[0]);
    return -1;
  }
  ssize_t nlines  = strtol(argv[1], NULL, 10);
  if (nlines==0 || nlines<0) {
    fprintf(stderr, "Invalid #lines (%ld)\n", nlines);
    return -1;
  }
  ssize_t nfiles  = strtol(argv[2], NULL, 10);
  if (nlines==0 || nlines<0) {
    fprintf(stderr, "Invalid #files (%ld)\n", nfiles);
    return -1;
  }
  // sanity check
  if (nfiles>maxnumfiles) {
    fprintf(stderr, "#files=%ld too big, max value is %d\n", nfiles, maxnumfiles);
    return -1;
  }
  struct stat statbuf;
  if (stat(argv[3], &statbuf)==-1) {
    perror("stat");
    fprintf(stderr, "Does the directory %s exit?\n", argv[3]);
    return -1;
  }
  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr, "%s is not a directory!\n", argv[3]);
    return -1;
  }
  char *dirname = argv[3];
  FILE **fp = calloc(sizeof(FILE*),nfiles);
  if (!fp) {
    perror("malloc");
    return -1;
  }
  char **buffer = calloc(IO_BUFFER, nfiles);
  if (!buffer) {
    perror("malloc");
    return -1;
  }
  int error=0;
  char filepath[strlen(dirname)+maxfilename];
  // opening (truncating) all files
  for(int i=0;i<nfiles;++i) {
    sprintf(filepath, fmtin, dirname, i); 
    fp[i] = fopen(filepath, "w");
    if (!fp[i]) {
      perror("fopen");
      fprintf(stderr, "cannot create (open) the file %s\n", filepath);
      error=-1;
    }    
    
    if (setvbuf(fp[i], buffer[i], _IOFBF, IO_BUFFER) != 0) {
      perror("setvbuf");
      return -1;
    }
  }
  if (!error) {
    char *buffer = calloc(maxphraselen, 1);
    if (!buffer) {
      perror("malloc");
      error=-1;
    }
    if (!error) {
      size_t cnt=0;
      for(ssize_t i=0;i<nlines;++i) {
	char *line = getrandomphrase(buffer, maxphraselen);
	size_t n= strlen(line);
	if (fwrite(line, 1, n, fp[cnt]) != n) {
	  perror("fwrite");
	  error = -1;
	  break;
	}
	cnt = (cnt+1) % nfiles;  // generiting one line for each file in a rr fashion
      }
    }
  }

  // closing all files
  for(int i=0;i<nfiles;++i) {
    if (fp[i]) fclose(fp[i]);
  }
  free(fp);

  gettimeofday(&after, NULL);
  double elapsed_time = diffmsec(after, before);
  fprintf(stdout, "SPLIT elapsed time (ms) : %g\n",elapsed_time);


  return error;

}
