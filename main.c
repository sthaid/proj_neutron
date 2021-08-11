#include <common.h>

//
// defines
//

#define MAX_DATA (100*86400)

#define MODE_CAPTURE  0
#define MODE_VIEW     1

//
// typedefs
//

//
// variables
//

static int    mode;
static FILE * fp_dat;
static time_t data_start_time;

static int    data[MAX_DATA];
static int    max_data;

//
// prototypes
//

static void initialize(int argc, char **argv);
static void update_display(int maxy, int maxx);
static int input_handler(int input_char);

//
// curses wrapper definitions
//

#define COLOR_PAIR_RED   1
#define COLOR_PAIR_CYAN  2

static bool      curses_active;
static bool      curses_term_req;
static pthread_t curses_thread_id;
static WINDOW  * curses_window;

static void curses_init(void);
static void curses_exit(void);
static void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char),
                           void (*other_handler)(void));

// -----------------  MAIN & INITIALIZE  -----------------------------------------

int main(int argc, char **argv)
{
    // initialize
    initialize(argc, argv);

    // invoke the curses user interface
    curses_init();
    curses_runtime(update_display, input_handler, NULL);
    curses_exit();

    // normal termination    
    INFO("terminating\n");
    return 0;
}

// xxx during this routine, log to both stderr and logfile
static void initialize(int argc, char **argv)
{
    char filename_dat[100];
    char s[100];

    #define USAGE \
    "usage: xxxx"

    // open log file, line buffered; and
    // log to stderr, just during initialize
    fp_log = fopen("neutron.log", "a");
    if (fp_log == NULL) {
        printf("FATAL: failed to open neutron.log, %s\n", strerror(errno));
        exit(1);
    }
    setlinebuf(fp_log);
    fp_log2 = stderr;

    // set mode and filename_dat to default
    mode = MODE_CAPTURE;
    sprintf(filename_dat, "neutron_%s.dat", time2str(s));
    
    // parse options
    // -v <filename.dat> : selects view mode, neutron data is not read from the ADC 
    // -h                : brief help
    while (true) {
        int ch = getopt(argc, argv, "v:h");
        if (ch == -1) {
            break;
        }
        switch (ch) {
        case 'v':
            mode = MODE_VIEW;
            strcpy(filename_dat, optarg);
            break;
        case 'h':
            printf("%s\n", USAGE);
            exit(0);
            break;
        default:
            exit(1);
        };
    }

    // log program starting
    INFO("-------- STARTING: MODE=%s FILENAME=%s --------\n",
         mode == MODE_VIEW ? "VIEW" : "CAPTURE",
         filename_dat);

    // if mode is VIEW then
    //   read the specified neutron_<date_time>.dat file
    // else 
    //   Construct neutron_xxxxxxxxxx.dat filename.
    //   Open and create the dat file.
    //   Init mccdaq device, used to acquire 500000 samples per second from the
    //    ludlum 2929 amplifier output.
    // endif
    if (mode == MODE_VIEW) {
        int    line=0, t;
        double v;

        // read filename_dat
        fp_dat = fopen(filename_dat, "r");
        if (fp_dat == NULL) {
            FATAL("open %s for reading, %s\n", filename_dat, strerror(errno));
        }
        while (fgets(s, sizeof(s), fp_dat) != NULL) {
            line++;
            if (line == 1) {
                if (sscanf(s, "# data_start_time = %ld", &data_start_time) != 1) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
            } else {
                if (sscanf(s, "%d %lf", &t, &v) != 2) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
                data[t] = v;
                max_data = t+1;
            }
        }
        fclose(fp_dat);
        fp_dat = NULL;
    } else {
        // init mccdaq utils
        int rc = mccdaq_init();
        if (rc < 0) {
            FATAL("mccdaq_init failed\n");
        }

        // create filename_dat for writing, and
        // write the data_start_time to the file
        fp_dat = fopen(filename_dat, "w");
        if (fp_dat == NULL) {
            FATAL("open %s for writing, %s\n", filename_dat, strerror(errno));
        }
        fprintf(fp_dat, "# data_start_time = %ld\n", time(NULL));

        // start acquiring ADC data from mccdaq utils
        mccdaq_start(mccdaq_callback);  // xxx maybe move this routine to this file
    }

    // stop logging to stderr
    fp_log2 = NULL;
}

char *time2str(char *s)
{
    // example: 2021-08-21_19:21:22
    time_t t = time(NULL);
    struct tm result;
    localtime_r(&t, &result);
    sprintf(s, "%4d-%02d-%02d_%02d:%02d:%02d",
            result.tm_year+1900, result.tm_mon+1, result.tm_mday, 
            result.tm_hour, result.tm_min, result.tm_sec);
    return s;
}

// -----------------  XXXXXXXXXXXXXXXXX  -----------------------------------------

// xxx pass other stats to here, like number of restarts
void publish_neutron_count(time_t time_now, int neutron_count) // xxx name
{
    int xxx = time_now - data_start_time;
    INFO("time = %d  neutron_count = %d\n", xxx, neutron_count);

    // xxx save in data array
}

void get_neutron_averaged_count(time_t t, int n)
{
    // assert that idx is not too big

    // if t is too early then return 0

    // compute the average over n samples
}

// -----------------  CURSES WRAPPER CALLBACKS  ----------------------------

static void update_display(int maxy, int maxx)
{
#if 0
    static int cnt;
    attron(COLOR_PAIR(COLOR_PAIR_CYAN));
    cnt++;
    mvprintw(0,0, "HELLO %d\n", cnt);
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN));
#endif

    // draw the x and y axis

    // loop over the number of display cols
    // - call get_neutron_averaged_count
    // - plot the data

}

static int input_handler(int input_char)
{
    // process input_char
    if (input_char == 4 || input_char == 'q') {  // 4 is ^d
        return -1;  // terminates pgm
    }

    // xxx other ctrls

    // return 0 means don't terminate pgm
    return 0;
}

// -----------------  CURSES WRAPPER  ----------------------------------------

static void curses_init(void)
{
    curses_active = true;
    curses_thread_id = pthread_self();

    curses_window = initscr();

    start_color();
    use_default_colors();
    init_pair(COLOR_PAIR_RED, COLOR_RED, -1);
    init_pair(COLOR_PAIR_CYAN, COLOR_CYAN, -1);

    cbreak();
    noecho();
    nodelay(curses_window,TRUE);
    keypad(curses_window,TRUE);
}

static void curses_exit(void)
{
    endwin();

    curses_active = false;
}

static void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char),
                           void (*other_handler)(void))
{
    int input_char, maxy, maxx;
    int maxy_last=0, maxx_last=0;
    int sleep_us;

    while (true) {
        // erase display
        erase();

        // get window size, and print whenever it changes
        getmaxyx(curses_window, maxy, maxx);
        if (maxy != maxy_last || maxx != maxx_last) {
            maxy_last = maxy;
            maxx_last = maxx;
        }

        // update the display
        update_display(maxy, maxx);

        // refresh display
        refresh();

        // process character inputs; 
        // note ERR means 'no input is waiting'
        sleep_us = 0;
        input_char = getch();
        if (input_char == KEY_RESIZE) {
            // immedeate redraw display
        } else if (input_char != ERR) {
            if (input_handler(input_char) != 0) {
                return;
            }
        } else {
            sleep_us = 100000;  // 100 ms
        }

        // if terminate curses request flag then return
        if (curses_term_req) {
            return;
        }

        // if other_handler is provided then call it
        // xxx delete ?
        if (other_handler) {
            other_handler();
        }

        // if need to sleep is indicated then do so
        if (sleep_us) {
            usleep(sleep_us);
        }
    }
}

