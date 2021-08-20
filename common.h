#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <curses.h>

// -----------------  LOGGING  -----------------------

#define MAX_VERBOSE 4
FILE *fp_log;
FILE *fp_log2;
bool verbose[MAX_VERBOSE];

#define PRINT_COMMON(lvl, fmt, args...) \
    do { \
        char s[100]; \
        fprintf(fp_log, "%s " lvl ": " fmt, time2str(time(NULL),s,false), ## args); \
        if (fp_log2) { \
            fprintf(fp_log2, "%s " lvl ": " fmt, time2str(time(NULL),s,false), ## args); \
        } \
    } while (0)

#define INFO(fmt, args...) PRINT_COMMON("INFO", fmt, ## args);
#define WARN(fmt, args...) PRINT_COMMON("WARN", fmt, ## args);
#define ERROR(fmt, args...) PRINT_COMMON("ERROR", fmt, ## args);

#define VERBOSE0(fmt, args...) do { if (verbose[0]) PRINT_COMMON("VERBOSE0", fmt, ## args); } while (0)
#define VERBOSE1(fmt, args...) do { if (verbose[1]) PRINT_COMMON("VERBOSE1", fmt, ## args); } while (0)
#define VERBOSE2(fmt, args...) do { if (verbose[2]) PRINT_COMMON("VERBOSE2", fmt, ## args); } while (0)
#define VERBOSE3(fmt, args...) do { if (verbose[3]) PRINT_COMMON("VERBOSE3", fmt, ## args); } while (0)

#define FATAL(fmt, args...) do { PRINT_COMMON("FATAL", fmt, ## args); exit(1); } while (0)

// -----------------  PROTOTYPES  -------------------

#define MAX_BUCKET        60
#define BUCKET_SIZE       5
#define MIN_PULSE_HEIGHT  20
#define MAX_PULSE_HEIGHT  (BUCKET_IDX_TO_PULSE_HEIGHT(MAX_BUCKET-1))
#define BUCKET_IDX_TO_PULSE_HEIGHT(bidx)  ((bidx) * BUCKET_SIZE)  // xxx use elsewhere
static inline int PULSE_HEIGHT_TO_BUCKET_IDX(int ph) {
    int bidx = ph / BUCKET_SIZE;
    return bidx < MAX_BUCKET ? bidx : MAX_BUCKET-1;
}

typedef struct {
    int bucket[MAX_BUCKET];
} pulse_count_t;

typedef int32_t (*mccdaq_callback_t)(uint16_t * data, int32_t max_data);

// main.c ...
void publish(time_t time_now, pulse_count_t *pc);

// mccdaq_cb.c ...
int32_t mccdaq_callback(uint16_t * d, int32_t max_d);

// util_mccdaq.c ...
int32_t mccdaq_init(void);
int32_t  mccdaq_start(mccdaq_callback_t cb);
int32_t  mccdaq_stop(void);
int32_t mccdaq_get_restart_count(void);

// utils.c ...
uint64_t microsec_timer(void);
char *time2str(time_t t, char *s, bool filename_format);

