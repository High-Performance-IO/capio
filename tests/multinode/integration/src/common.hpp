#ifndef COMMON_HPP
#define COMMON_HPP
#include <capio/utils.h>
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

char **build_args() {
    char **args = (char **) malloc(3 * sizeof(uintptr_t));

    char const *command = std::getenv("CAPIO_SERVER_PATH");
    if (command == nullptr) {
        command = "capio_server";
    }

    args[0] = strdup(command);
    args[1] = strdup("--no-config");
    args[2] = (char *) nullptr;

    return args;
}

char **build_env(char **envp) {
    std::vector<int> vars;
    for (int i = 0; envp[i] != nullptr; i++) {
        const std::string_view var(envp[i]);
        const std::string_view key = var.substr(0, var.find('='));
        if (key != "LD_PRELOAD") {
            vars.emplace_back(i);
        }
    }

    char **cleaned_env = (char **) malloc((vars.size() + 2) * sizeof(uintptr_t));
    for (int i = 0; i < vars.size(); i++) {
        cleaned_env[i] = strdup(envp[i]);
    }
    cleaned_env[vars.size()]     = strdup("LD_PRELOAD=");
    cleaned_env[vars.size() + 1] = (char *) nullptr;

    return cleaned_env;
}

class CapioServerEnvironment : public testing::Environment {
  private:
    char **args;
    char **envp;
    int server_pid;

  public:
    explicit CapioServerEnvironment(char **envp) : args(build_args()), envp(build_env(envp)) {
        server_pid = -1;
    };

    ~CapioServerEnvironment() override {
        for (int i = 0; args[i] != nullptr; i++) {
            free(args[i]);
        }
        free(args);
        for (int i = 0; envp[i] != nullptr; i++) {
            free(envp[i]);
        }
        free(envp);
    };

    void SetUp() override {
        if (server_pid < 0) {
            ASSERT_NE(std::getenv("CAPIO_DIR"), nullptr);
            ASSERT_GE(server_pid = fork(), 0);
            if (server_pid == 0) {
                execvpe(args[0], args, envp);
                _exit(127);
            } else {
                sleep(5);
            }
        }
    }

    void TearDown() override {
        if (server_pid > 0) {
            kill(server_pid, SIGTERM);
            waitpid(server_pid, nullptr, 0);
            server_pid = -1;
        }
    }
};

class CapioEnvironmentHandler : public testing::EmptyTestEventListener {

    CapioServerEnvironment *_env;

    // Called before a test starts.
    void OnTestStart(const testing::TestInfo &test_info) override { _env->SetUp(); }

    // Called after a test ends.
    void OnTestEnd(const testing::TestInfo &test_info) override { _env->TearDown(); }

  public:
    explicit CapioEnvironmentHandler(char **envp)
        : testing::EmptyTestEventListener(), _env(new CapioServerEnvironment(envp)) {}
};

#endif // COMMON_HPP
