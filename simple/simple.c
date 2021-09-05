#include <common.h>

// variables
static bool term_pgm;
static int  pulse_count = -1;

// prototypes
static void sig_hndlr(int sig);

// ----------------------------------------------------

int main(int argc, char **argv)
{
    //
    // Initialization
    //

    // open logfile in append mode
    fp_log = fopen("simple.log", "a");
    if (fp_log == NULL) {
        printf("FATAL: failed to open simple.log, %s\n", strerror(errno));
        exit(1);
    }
    setlinebuf(fp_log);
    INFO("STARTING\n");

    // register signal handler, used to terminate program gracefully
    static struct sigaction act;
    act.sa_handler = sig_hndlr;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    // enable verbose logging:
    // - verbose[0]: stats every TIME_INTVL_SECS
    // - verbose[1]: pulse, but not more than once per second
    // - verbose[3]: prints in util_mccdaq.c
    verbose[1] = true;

    // init mccdaq utils, and
    // start acquiring ADC data using mccdaq utils
    if (mccdaq_init() < 0) {
        printf("FATAL: mccdaq_init failed\n");
        exit(1);
    }
    mccdaq_start(mccdaq_callback);

    //
    // Run time
    //

    while (true) {
        // if ctrl-c then terminate program
        if (term_pgm) {
            break;
        }

        // if a pulse_count was published then print and log it
        if (pulse_count != -1) {
            char s[100];
            printf("%s  %0.1f CPM\n", time2str(time(NULL),s,false), pulse_count*(60./TIME_INTVL_SECS));
            INFO("PULSE_COUNT = %0.1f CPM\n", pulse_count * (60./TIME_INTVL_SECS));
            pulse_count = -1;
        }

        // sleep for 100 ms
        usleep(100000);
    }

    // exit
    INFO("TERMINATING\n");
    return 0;
}

void publish(time_t time_now, int pulse_count_arg)
{
    pulse_count = pulse_count_arg;
}

static void sig_hndlr(int sig)
{
    term_pgm = true;
}

