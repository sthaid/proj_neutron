#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <curses.h>
#include <math.h>

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
#define VERBOSE2(fmt, args...) do { if (verbose[2]) PRINT_COMMON("VERBOSE2", fmt, ## args); } while (3)
#define VERBOSE3(fmt, args...) do { if (verbose[3]) PRINT_COMMON("VERBOSE3", fmt, ## args); } while (0)

#define FATAL(fmt, args...) do { PRINT_COMMON("FATAL", fmt, ## args); exit(1); } while (0)

// -----------------  PROTOTYPES  -------------------

// main.c ...
void set_neutron_count(time_t time_now, int neutron_count);
char *time2str(time_t t, char *s, bool filename_format);

// mccdaq_cb.c ...
int32_t mccdaq_callback(uint16_t * d, int32_t max_d);

// util_mccdaq.c ...
typedef int32_t (*mccdaq_callback_t)(uint16_t * data, int32_t max_data);  // xxx elim
int32_t mccdaq_init(void);
int32_t  mccdaq_start(mccdaq_callback_t cb);
int32_t  mccdaq_stop(void);
int32_t mccdaq_get_restart_count(void);

