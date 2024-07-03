#include <gtest/gtest.h>

#include "mapreduce.hpp"

static int writedata(char *dataptr, size_t datalen, float percent, char *destdir, ssize_t dstart,
                     ssize_t dfiles) {
    int error = 0;
    FILE **fp = (FILE **) calloc(sizeof(FILE *), dfiles);
    if (!fp) {
        perror("malloc");
        return -1;
    }
    char filepath[strlen(destdir) + maxfilename];

    // opening (truncating) all files
    for (int j = 0, i = 0 + dstart; i < (dfiles + dstart); ++i, ++j) {
        sprintf(filepath, fmtout, destdir, i);
        fp[j] = fopen(filepath, "w");
        if (!fp[j]) {
            perror("fopen");
            fprintf(stderr, "cannot create (open) the file %s\n", filepath);
            error = -1;
        }
    }

    if (!error) {
        size_t nbytes = datalen * percent;
        size_t cnt    = 0;
        while (nbytes > 0) {
            size_t chunk = (nbytes > REDUCE_CHUNK) ? REDUCE_CHUNK : nbytes;
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
    for (int i = 0; i < dfiles; ++i) {
        if (fp[i]) {
            fclose(fp[i]);
        }
    }
    free(fp);
    return error;
}

static char *getrandomphrase(char *buffer, size_t len) {
    static int phrases_entry = sizeof(phrases) / sizeof(phrases[0]);

    bzero(buffer, len);

    int r;
    switch ((r = rand()) % 4) {
    case 0: {
        strncat(buffer, phrases[r % phrases_entry], len - 1);
        break;
    }
    case 1: {
        strncat(buffer, phrases[r % phrases_entry], len - 1);
        ssize_t l = len - strlen(buffer) - 1;
        if (l > 0) {
            strncat(buffer, phrases[rand() % phrases_entry], l);
        }
        break;
    }
    case 2: {
        strncat(buffer, phrases[r % phrases_entry], len - 1);
        ssize_t l = len - strlen(buffer) - 1;
        if (l > 0) {
            strncat(buffer, phrases[rand() % phrases_entry], l);
        }
        l = len - strlen(buffer) - 1;
        if (l > 0) {
            strncat(buffer, phrases[rand() % phrases_entry], l);
        }
        break;
    }
    case 3: {
        strncat(buffer, phrases[r % phrases_entry], len - 1);
        break;
    }
    }
    buffer[strlen(buffer)] = '\n';

    return buffer;
}

int mergeFunction(int argc, char *argv[]) {
    (void) fmtin;
    (void) phrases;

    struct timeval before, after;
    gettimeofday(&before, NULL);

    if (argc != 4) {
        fprintf(stderr, "use: %s #files sourcedir destdir\n", argv[0]);
        return -1;
    }
    ssize_t nfiles = strtol(argv[1], NULL, 10);
    if (nfiles == 0 || nfiles < 0) {
        fprintf(stderr, "Invalid #files (%ld)\n", nfiles);
        return -1;
    }
    struct stat statbuf;
    if (stat(argv[2], &statbuf) == -1) {
        perror("stat");
        fprintf(stderr, "Does the directory %s exit?\n", argv[2]);
        return -1;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", argv[2]);
        return -1;
    }
    if (stat(argv[3], &statbuf) == -1) {
        perror("stat");
        fprintf(stderr, "Does the directory %s exit?\n", argv[3]);
        return -1;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", argv[3]);
        return -1;
    }

    char *sourcedir     = argv[2];
    char *destdir       = argv[3];
    char *dataptr       = NULL;
    size_t datalen      = 0;
    size_t datacapacity = 0;
    char filepath[strlen(sourcedir) + maxfilename];
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtout, sourcedir, i);
        FILE *fp = fopen(filepath, "r");
        if (!fp) {
            perror("fopen");
            fprintf(stderr, "cannot open the file %s\n", filepath);
            return -1;
        }
        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        if (ptr == NULL) {
            free(dataptr);
            return -1;
        }
        dataptr = ptr;
        fclose(fp);
    }
    int error = 0;
    char resultpath[strlen(destdir) + strlen("/result.dat")];
    sprintf(resultpath, "%s/result.dat", destdir);
    FILE *fp = fopen(resultpath, "w");
    if (!fp) {
        perror("fopen");
        fprintf(stderr, "cannot creat %s\n", resultpath);
        error = -1;
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

int splitFunction(int argc, char *argv[]) {
    struct timeval before, after;
    gettimeofday(&before, NULL);

    (void) fmtout;
    if (argc != 4) {
        fprintf(stderr, "use: %s #lines #files destdir\n", argv[0]);
        return -1;
    }
    ssize_t nlines = strtol(argv[1], NULL, 10);
    if (nlines == 0 || nlines < 0) {
        fprintf(stderr, "Invalid #lines (%ld)\n", nlines);
        return -1;
    }
    ssize_t nfiles = strtol(argv[2], NULL, 10);
    if (nlines == 0 || nlines < 0) {
        fprintf(stderr, "Invalid #files (%ld)\n", nfiles);
        return -1;
    }
    // sanity check
    if (nfiles > maxnumfiles) {
        fprintf(stderr, "#files=%ld too big, max value is %d\n", nfiles, maxnumfiles);
        return -1;
    }
    struct stat statbuf;
    if (stat(argv[3], &statbuf) == -1) {
        perror("stat");
        fprintf(stderr, "Does the directory %s exit?\n", argv[3]);
        return -1;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", argv[3]);
        return -1;
    }
    char *dirname = argv[3];
    FILE **fp     = (FILE**)calloc(sizeof(FILE *), nfiles);
    if (!fp) {
        perror("malloc");
        return -1;
    }
    char **buffer = (char**)calloc(IO_BUFFER, nfiles);
    if (!buffer) {
        perror("malloc");
        return -1;
    }
    int error = 0;
    char filepath[strlen(dirname) + maxfilename];
    // opening (truncating) all files
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtin, dirname, i);
        fp[i] = fopen(filepath, "w");
        if (!fp[i]) {
            perror("fopen");
            fprintf(stderr, "cannot create (open) the file %s\n", filepath);
            error = -1;
        }

        if (setvbuf(fp[i], buffer[i], _IOFBF, IO_BUFFER) != 0) {
            perror("setvbuf");
            return -1;
        }
    }
    if (!error) {
        char *buffer = (char *) calloc(maxphraselen, 1);
        if (!buffer) {
            perror("malloc");
            error = -1;
        }
        if (!error) {
            size_t cnt = 0;
            for (ssize_t i = 0; i < nlines; ++i) {
                char *line = getrandomphrase(buffer, maxphraselen);
                size_t n   = strlen(line);
                if (fwrite(line, 1, n, fp[cnt]) != n) {
                    perror("fwrite");
                    error = -1;
                    break;
                }
                cnt = (cnt + 1) % nfiles; // generiting one line for each file in a rr fashion
            }
        }
    }

    // closing all files
    for (int i = 0; i < nfiles; ++i) {
        if (fp[i]) {
            fclose(fp[i]);
        }
    }
    free(fp);

    gettimeofday(&after, NULL);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "SPLIT elapsed time (ms) : %g\n", elapsed_time);

    return error;
}

int mapReduceFunction(int argc, char *argv[]) {
    (void) phrases;

    struct timeval before, after;
    gettimeofday(&before, NULL);

    if (argc != 8) {
        fprintf(stderr, "use: %s sourcedir #sstart #sfiles destdir #dstart #dfiles percent\n",
                argv[0]);
        return -1;
    }
    struct stat statbuf;
    if (stat(argv[1], &statbuf) == -1) {
        perror("stat source");
        fprintf(stderr, "Does the directory %s exit?\n", argv[1]);
        return -1;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", argv[1]);
        return -1;
    }
    char *sourcedirname = argv[1];
    ssize_t sstart      = strtol(argv[2], NULL, 10);
    if (sstart < 0) {
        fprintf(stderr, "Invalid #sstart (%ld)\n", sstart);
        return -1;
    }
    ssize_t sfiles = strtol(argv[3], NULL, 10);
    if (sfiles == 0 || sfiles < 0) {
        fprintf(stderr, "Invalid #sfiles (%ld)\n", sfiles);
        return -1;
    }
    if (stat(argv[4], &statbuf) == -1) {
        perror("stat destination");
        fprintf(stderr, "Does the directory %s exit?\n", argv[4]);
        return -1;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "%s is not a directory!\n", argv[4]);
        return -1;
    }
    char *destdirname = argv[4];
    ssize_t dstart    = strtol(argv[5], NULL, 10);
    if (dstart < 0) {
        fprintf(stderr, "Invalid #dstart (%ld)\n", dstart);
        return -1;
    }
    ssize_t dfiles = strtol(argv[6], NULL, 10);
    if (dfiles == 0 || dfiles < 0) {
        fprintf(stderr, "Invalid #dfiles (%ld)\n", dfiles);
        return -1;
    }
    float percent = strtof(argv[7], NULL);
    if (percent > 1 || percent <= 0) {
        fprintf(stderr, "Invalid percent (%f)\n", percent);
        return -1;
    }

    char *dataptr       = NULL;
    size_t datalen      = 0;
    size_t datacapacity = 0;
    char filepath[strlen(sourcedirname) + maxfilename];
    // concatenating all files in memory (dataptr)
    for (int i = 0 + sstart; i < (sfiles + sstart); ++i) {
        sprintf(filepath, fmtin, sourcedirname, i);
        FILE *fp = fopen(filepath, "r");
        if (!fp) {
            perror("fopen");
            fprintf(stderr, "cannot open the file %s\n", filepath);
            return -1;
        }
        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        if (ptr == NULL) {
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

TEST(integrationTests, RunTestSplitMergeAndMapReduceFunction) {

}