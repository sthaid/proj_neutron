#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <signal.h>
#include <pthread.h>
#include <curses.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// -----------------  LOGGING  -----------------------

#define MAX_VERBOSE 4
bool verbose[MAX_VERBOSE];
FILE *fp_log;

#define PRINT_COMMON(lvl, fmt, args...) \
    do { \
        char _s[100]; \
        fprintf(fp_log, "%s " lvl ": " fmt, time2str(time(NULL),_s,false), ## args); \
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

// tuning values
#define TIME_INTVL_SECS   5
#define MIN_PULSE_HEIGHT  40

typedef int32_t (*mccdaq_callback_t)(uint16_t * data, int32_t max_data);

// main.c ...
void publish(time_t time_now, int pulse_count);

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

