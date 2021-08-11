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

FILE *fp_log;
FILE *fp_log2;

#define INFO(fmt, args...) \
    do { \
        char s[100]; \
        fprintf(fp_log, "%s INFO: " fmt, time2str(s), ## args); \
        if (fp_log2) { \
            fprintf(fp_log2, "%s INFO: " fmt, s, ## args); \
        } \
    } while (0)
#define WARN(fmt, args...) \
    do { \
        char s[100]; \
        fprintf(fp_log, "%s WARN: " fmt, time2str(s), ## args); \
        if (fp_log2) { \
            fprintf(fp_log2, "%s WARN: " fmt, s, ## args); \
        } \
    } while (0)
#define ERROR(fmt, args...) \
    do { \
        char s[100]; \
        fprintf(fp_log, "%s ERROR: " fmt, time2str(s), ## args); \
        if (fp_log2) { \
            fprintf(fp_log2, "%s ERROR: " fmt, s, ## args); \
        } \
    } while (0)
#define FATAL(fmt, args...) \
    do { \
        char s[100]; \
        fprintf(fp_log, "%s FATAL: " fmt, time2str(s), ## args); \
        if (fp_log2) { \
            fprintf(fp_log2, "%s FATAL: " fmt, s, ## args); \
        } \
        exit(1); \
    } while (0)

#if 0 // xxx fix
#define DEBUG(fmt, args...) \
    do { \
        printf("DEBUG: " fmt, ## args); \
    } while (0)
#else
#define DEBUG(fmt, args...)
#endif

// -----------------  PROTOTYPES  -------------------

// from main.c
void publish_neutron_count(time_t time_now, int neutron_count);
char *time2str(char *s);

// from mccdaq_cb.c
int32_t mccdaq_callback(uint16_t * d, int32_t max_d);

// from util_mccdaq.c
typedef int32_t (*mccdaq_callback_t)(uint16_t * data, int32_t max_data);  // xxx elim
int32_t mccdaq_init(void);
int32_t  mccdaq_start(mccdaq_callback_t cb);
int32_t  mccdaq_stop(void);
int32_t mccdaq_get_restart_count(void);

