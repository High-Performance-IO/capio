#if !defined(_MAPREDUCE_H)
#define _MAPREDUCE_H

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
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
    char *buffer = malloc(maxphraselen);
    if (!buffer) {
        perror("malloc");
        return NULL;
    }
    size_t len = maxphraselen - 1;

    if (dataptr == 0) {
        dataptr = realloc(dataptr, REALLOC_BATCH);
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
            char *tmp = realloc(dataptr, *datacapacity);
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

#endif // _MAPREDUCE_H