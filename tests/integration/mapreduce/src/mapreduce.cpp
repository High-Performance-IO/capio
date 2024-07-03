#include <gtest/gtest.h>

#include <assert.h>
#include <errno.h>
#include <future>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

// just a bunch of random phrases
static const char *phrases[] = {
    "This is short phrase!",
    "I never knew what hardship looked like until it started raining bowling balls.",
    "The clock within this blog and the clock on my laptop are 1 hour different from each other.",
    "The glacier came alive as the climbers hiked closer.",
    "Mom didn't understand why no one else wanted a hot tub full of jello.",
    "The reservoir water level continued to lower while we enjoyed our long shower.",
    "I was very proud of my nickname throughout high school but today- I couldn’t be any different "
    "to what my nickname was.",
    "In Pisa there is the leaning tower!",
    "Dolores wouldn't have eaten the meal if she had known what it actually was.",
    "He enjoys practicing his ballet in the bathroom.",
    "It isn't true that my mattress is made of cotton candy.",
    "The hand sanitizer was actually clear glue.",
    "Can I generate meaningful random sentences?",
    "Sometimes a random word just isn't enough, and that is where the random sentence generator "
    "comes into play. By inputting the desired number, you can make a list of as many random "
    "sentences as you want or need. Producing random sentences can be helpful in a number of "
    "different ways.",
    "Bye!",
    "The memory we used to share is no longer coherent.",
    "The tour bus was packed with teenage girls heading toward their next adventure.",
    "The small white buoys marked the location of hundreds of crab pots.",
    "The wake behind the boat told of the past while the open sea for told life in the unknown "
    "future.",
    "Going from child, to childish, to childlike is only a matter of time.",
    "The beauty of the African sunset disguised the danger lurking nearby.",
    "Little Red Riding Hood decided to wear orange today.",
    "I honestly find her about as intimidating as a basket of kittens."
    "Hello world!"};

static const int maxphraselen  = 1024;
static const int maxfilename   = 32;
static const int maxnumfiles   = 10000;
static char fmtin[]            = "%s/infile_%05d.dat"; // 5 is the number of digits of maxnumfiles
static char fmtout[]           = "%s/outfile_%05d.dat";
static const int REALLOC_BATCH = 10485760; // 10MB
static const int REDUCE_CHUNK  = 10240;    // 10K
static const int IO_BUFFER     = 1048576;  // 1MB

// reading file data into memory
static inline char *readdata(FILE *fp, char *dataptr, size_t *datalen, size_t *datacapacity) {
    char *buffer = (char *) malloc(maxphraselen);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }
    size_t len = maxphraselen - 1;

    if (dataptr == 0) {
        dataptr = (char *) realloc(dataptr, REALLOC_BATCH);
        if (dataptr == NULL) {
            perror("realloc");
            return NULL;
        }
        *datacapacity = REALLOC_BATCH;
        *datalen      = 0;
    }
    ssize_t r;
    while (errno = 0, (r = getline(&buffer, &len, fp)) > 0) {
        if ((*datalen + r) > *datacapacity) {
            *datacapacity += REALLOC_BATCH;
            char *tmp = (char *) realloc(dataptr, *datacapacity);
            if (tmp == NULL) {
                perror("realloc");
                free(dataptr);
                return NULL;
            }
            dataptr = tmp;
        }
        strncpy(&dataptr[*datalen], buffer, maxphraselen);
        *datalen += r;
    }
    if (errno != 0) {
        perror("getline");
        free(dataptr);
        return NULL;
    }
    return dataptr;
}

static inline double diffmsec(const struct timeval a, const struct timeval b) {
    long sec  = (a.tv_sec - b.tv_sec);
    long usec = (a.tv_usec - b.tv_usec);

    if (usec < 0) {
        --sec;
        usec += 1000000;
    }
    return ((double) (sec * 1000) + ((double) usec) / 1000.0);
}

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

int mergeFunction(ssize_t nfiles, char *sourcedir, char *destdir) {

    struct timeval before, after;
    struct stat statbuf;
    char *dataptr  = NULL;
    size_t datalen = 0, datacapacity = 0;
    gettimeofday(&before, NULL);

    EXPECT_GT(nfiles, 0);

    EXPECT_NE(stat(sourcedir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    EXPECT_NE(stat(destdir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    char filepath[strlen(sourcedir) + maxfilename];
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtout, sourcedir, i);
        FILE *fp = fopen(filepath, "r");
        EXPECT_TRUE(fp);

        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        EXPECT_NE(ptr, nullptr);

        dataptr = ptr;
        fclose(fp);
    }

    char resultpath[strlen(destdir) + strlen("/result.dat")];
    sprintf(resultpath, "%s/result.dat", destdir);
    FILE *fp = fopen(resultpath, "w");
    EXPECT_TRUE(fp);

    EXPECT_EQ(fwrite(dataptr, 1, datalen, fp), datalen);

    free(dataptr);

    gettimeofday(&after, NULL);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "MERGE: elapsed time (ms) : %g\n", elapsed_time);

    return 0;
}

int splitFunction(ssize_t nlines, ssize_t nfiles, char *dirname) {
    struct timeval before, after;
    gettimeofday(&before, NULL);
    EXPECT_GT(nlines, 0);
    EXPECT_GT(nfiles, 0);

    // sanity check
    EXPECT_LE(nfiles, maxnumfiles);

    struct stat statbuf;
    EXPECT_NE(stat(dirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode)); // not a directory

    FILE **fp = (FILE **) calloc(sizeof(FILE *), nfiles);
    EXPECT_TRUE(fp);

    char **buffer = (char **) calloc(IO_BUFFER, nfiles);
    EXPECT_TRUE(buffer);

    int error = 0;
    char filepath[strlen(dirname) + maxfilename];
    // opening (truncating) all files
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtin, dirname, i);
        fp[i] = fopen(filepath, "w");

        EXPECT_TRUE(fp[i]);
        EXPECT_EQ(setvbuf(fp[i], buffer[i], _IOFBF, IO_BUFFER), 0);
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

int mapReduceFunction(char *sourcedirname, ssize_t sstart, ssize_t sfiles, char *destdirname,
                      ssize_t dstart, ssize_t dfiles, float percent) {
    struct timeval before, after;
    struct stat statbuf;
    char *dataptr       = NULL;
    size_t datalen      = 0;
    size_t datacapacity = 0;
    gettimeofday(&before, NULL);

    EXPECT_NE(stat(sourcedirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_GE(sstart, 0);
    EXPECT_GT(sfiles, 0);
    EXPECT_NE(stat(destdirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_GE(dstart, 0);
    EXPECT_GT(dfiles, 0);
    EXPECT_GT(percent, 0);
    EXPECT_LE(percent, 1);

    char filepath[strlen(sourcedirname) + maxfilename];
    // concatenating all files in memory (dataptr)
    for (int i = 0 + sstart; i < (sfiles + sstart); ++i) {
        sprintf(filepath, fmtin, sourcedirname, i);

        FILE *fp = fopen(filepath, "r");
        EXPECT_TRUE(fp);

        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        EXPECT_NE(ptr, nullptr);

        dataptr = ptr;
        fclose(fp);
    }

    int r = writedata(dataptr, datalen, percent, destdirname, dstart, dfiles);
    free(dataptr);

    gettimeofday(&after, NULL);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "MAPREDUCE: elapsed time (ms) : %g\n", elapsed_time);

    EXPECT_EQ(r, 0);

    return r;
}

TEST(integrationTests, RunTestSplitMergeAndMapReduceFunction) {

    EXPECT_EQ(splitFunction(10, 10, std::getenv("CAPIO_DIR")), 0);
    std::thread mapReducer1(mapReduceFunction, std::getenv("CAPIO_DIR"), 0, 5,
                            std::getenv("CAPIO_DIR"), 0, 5, 0.3);
    std::thread mapReducer2(mapReduceFunction, std::getenv("CAPIO_DIR"), 1, 5,
                            std::getenv("CAPIO_DIR"), 1, 5, 0.3);

    mapReducer1.join();
    mapReducer2.join();

    EXPECT_EQ(mergeFunction(2, std::getenv("CAPIO_DIR"), std::getenv("CAPIO_DIR")), 0);
}
