#include <common.h>

uint64_t microsec_timer(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC,&ts);
    return  ((uint64_t)ts.tv_sec * 1000000) + ((uint64_t)ts.tv_nsec / 1000);
}

char *time2str(time_t t, char *s, bool filename_format)
{
    // example: 
    // - filename_format == true:   2021-08-21_19-21-22
    // - filename_format == false:  2021/08/21 19:21:22

    struct tm result;
    localtime_r(&t, &result);
    if (filename_format) {
        sprintf(s, "%4d-%02d-%02d_%02d-%02d-%02d",
                result.tm_year+1900, result.tm_mon+1, result.tm_mday,
                result.tm_hour, result.tm_min, result.tm_sec);
    } else {
        sprintf(s, "%4d/%02d/%02d %02d:%02d:%02d",
                result.tm_year+1900, result.tm_mon+1, result.tm_mday,
                result.tm_hour, result.tm_min, result.tm_sec);
    }
    return s;
}

