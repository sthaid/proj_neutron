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

// -----------------  LOGGING  -----------------------

extern FILE *fplog;

#define INFO(fmt, args...) \
    do { \
        fprintf(fplog, "INFO: " fmt, ## args); \
    } while (0)
#define WARN(fmt, args...) \
    do { \
        fprintf(fplog, "WARN: " fmt, ## args); \
    } while (0)
#define ERROR(fmt, args...) \
    do { \
        fprintf(fplog, "ERROR: " fmt, ## args); \
    } while (0)
#define FATAL(fmt, args...) \
    do { \
        fprintf(fplog, "FATAL: " fmt, ## args); \
        exit(1); \
    } while (0)

#if 0
#define DEBUG(fmt, args...) \
    do { \
        printf("DEBUG: " fmt, ## args); \
    } while (0)
#else
#define DEBUG(fmt, args...)
#endif

// -----------------  MCCDAQ CB  --------------------

int32_t mccdaq_callback(uint16_t * d, int32_t max_d);

// -----------------  MCCDAQ UTILS  -----------------

typedef int32_t (*mccdaq_callback_t)(uint16_t * data, int32_t max_data);

int32_t mccdaq_init(void);
int32_t  mccdaq_start(mccdaq_callback_t cb);
int32_t  mccdaq_stop(void);
int32_t mccdaq_get_restart_count(void);


