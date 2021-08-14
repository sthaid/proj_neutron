#include <common.h>

//
// defines
//

#define MAX_CPS (100*86400)

#define MODE_LIVE      0
#define MODE_PLAYBACK  1

#define MODE_STR(x) \
    ((x) == MODE_LIVE     ? "LIVE"     : \
     (x) == MODE_PLAYBACK ? "PLAYBACK"   \
                          : "????")

//
// typedefs
//

//
// variables
//

static int       mode;
static bool      tracking;
static FILE    * fp_dat;
static bool      program_terminating;
static pthread_t live_mode_write_cps_data_thread_id;
static char      filename_dat[200];

static time_t    cps_data_start_time;
static int       cps_data[MAX_CPS];
static int       max_cps_data;

//
// prototypes
//

static void initialize(int argc, char **argv);
static void * live_mode_write_cps_data_thread(void *cx);

static void update_display(int maxy, int maxx);
static double get_average_neutron_cps(int time_idx, int num_avg_secs);
static int input_handler(int input_char);
static void clip_value(int *v, int min, int max);

//
// curses wrapper definitions
//

#define COLOR_PAIR_RED   1
#define COLOR_PAIR_GREEN 2
#define COLOR_PAIR_CYAN  3

static bool      curses_active;
static bool      curses_term_req;
static pthread_t curses_thread_id;
static WINDOW  * curses_window;

static void curses_init(void);
static void curses_exit(void);
static void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char));

// -----------------  MAIN & INITIALIZE  -----------------------------------------

int main(int argc, char **argv)
{
    // initialize
    initialize(argc, argv);

    // invoke the curses user interface
    curses_init();
    curses_runtime(update_display, input_handler);
    curses_exit();

    // program terminating
    INFO("terminating\n");
    program_terminating = true;
    if (mode == MODE_LIVE) {
        assert(live_mode_write_cps_data_thread_id != 0);
        pthread_join(live_mode_write_cps_data_thread_id, NULL);
    }
    return 0;
}

static void initialize(int argc, char **argv)
{
    char s[100];

    #define USAGE "usage: neutron [-p <filename.dat] [-v <select>] [-h]\n" \
                  "        -p <filename.dat> : playback\n" \
                  "        -v <select>       : enable verbose logging, select=0,1,2,3,all\n" \
                  "        -h                : help\n"

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
    mode = MODE_LIVE;
    sprintf(filename_dat, "neutron_%s.dat", time2str(time(NULL),s,true));
    
    // parse options
    // -v <filename.dat> : selects view mode, neutron data is not read from the ADC 
    // -h                : brief help
    while (true) {
        int ch = getopt(argc, argv, "p:v:h");
        if (ch == -1) {
            break;
        }
        switch (ch) {
        case 'p':
            mode = MODE_PLAYBACK;
            strcpy(filename_dat, optarg);
            break;
        case 'v':
            if (strcmp(optarg, "all") == 0) {
                memset(verbose, 1, MAX_VERBOSE);
            } else {
                int cnt, select;
                cnt = sscanf(optarg, "%d", &select);
                if (cnt != 1 || select < 0 || select >= MAX_VERBOSE) {
                    FATAL("invalid verbose select '%s'\n", optarg);
                }
                verbose[select] = true;
            }
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
         MODE_STR(mode), filename_dat);

    // if mode is PLAYBACK then
    //   read data from filename_dat
    // else 
    //   Create filename_dat, to save the neutron count data.
    //   Initialize the ADC, and start the ADC acquiring data from the Ludlum.
    //     Note: Init mccdaq device, is used to acquire 500000 samples per second from the
    //           Ludlum 2929 amplifier output.
    // endif
    if (mode == MODE_PLAYBACK) {
        int line=0, time_idx, cps_data_val;

        // read filename_dat
        fp_dat = fopen(filename_dat, "r");
        if (fp_dat == NULL) {
            FATAL("open %s for reading, %s\n", filename_dat, strerror(errno));
        }
        while (fgets(s, sizeof(s), fp_dat) != NULL) {
            line++;
            if (line == 1) {
                if (sscanf(s, "# cps_data_start_time = %ld", &cps_data_start_time) != 1) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
            } else {
                if (sscanf(s, "%d %d", &time_idx, &cps_data_val) != 2) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
                cps_data[time_idx] = cps_data_val;
                max_cps_data = time_idx+1;
            }
        }
        fclose(fp_dat);
        fp_dat = NULL;
        INFO("max_cps_data = %d\n", max_cps_data);
    } else {
        // init mccdaq utils
        int rc = mccdaq_init();
        if (rc < 0) {
            FATAL("mccdaq_init failed\n");
        }

        // create filename_dat for writing, and
        // write the cps_data_start_time to the file
        fp_dat = fopen(filename_dat, "w");
        if (fp_dat == NULL) {
            FATAL("open %s for writing, %s\n", filename_dat, strerror(errno));
        }
        cps_data_start_time = time(NULL);
        fprintf(fp_dat, "# cps_data_start_time = %ld\n", cps_data_start_time);

        // create thread that will write the neutron count data to
        // the neutron.dat file
        pthread_create(&live_mode_write_cps_data_thread_id, NULL, 
                       live_mode_write_cps_data_thread, NULL);

        // start acquiring ADC data from mccdaq utils
        mccdaq_start(mccdaq_callback);

        // enable tracking, this applies only to MODE_LIVE, and causes
        // update_display to display the latest neutron count data
        tracking = true;
    }

    // stop logging to stderr
    fp_log2 = NULL;
}

char *time2str(time_t t, char *s, bool filename_format)
{
    // example: 2021-08-21_19-21-22
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

// -----------------  SET NEUTRON COUNT  -----------------------------------------

// called from mccdaq_cb at 1 second intervals, with neutron count for the past second

void live_mode_set_neutron_count(time_t time_now, int neutron_count)
{
    // determine data array time_idx, and sanity check
    int time_idx = time_now - cps_data_start_time;
    if (time_idx < 0 || time_idx >= MAX_CPS) {
        FATAL("time_idx=%d out of range 0..MAX_CPS-1\n", time_idx);
    }

    // sanity check, that the time_now is 1 greater than at last call
    static time_t time_last;
    time_t delta_time = time_now - time_last;
    if (time_last != 0 && delta_time != 1) {
        WARN("unexpected delta_time %ld, should be 1\n", delta_time);
    }
    time_last = time_now;

    // save neutron_count in data array
    cps_data[time_idx] = neutron_count;
    __sync_synchronize();
    max_cps_data = time_idx+1;
}

static void * live_mode_write_cps_data_thread(void *cx)
{
    int        time_idx;
    bool       terminate;
    static int last_time_idx_written = -1;

    // file should already been opened in initialize()
    assert(fp_dat);

    // loop, writing data to neutron.dat file
    while (true) {
        // read program_terminating flag prior to writing to the file
        terminate = program_terminating;

        // write new neutron count data entries to the file
        for (time_idx = last_time_idx_written+1; time_idx < max_cps_data; time_idx++) {
            fprintf(fp_dat, "%6d %6d\n", time_idx, cps_data[time_idx]);
            last_time_idx_written = time_idx;
        }

        // if terminate has been requested then break
        if (terminate) {
            break;
        }

        // sleep 1 sec
        sleep(1);
    }

    // close the file, and
    // exit this thread
    fclose(fp_dat);
    return NULL;
}

// -----------------  CURSES WRAPPER CALLBACKS  ----------------------------

// y axis scale
#define DEFAULT_Y_MAX_TBL_IDX  10
#define Y_MAX (y_max_tbl[y_max_tbl_idx])
static int y_max_tbl[] = { 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000, };
static int y_max_tbl_idx = DEFAULT_Y_MAX_TBL_IDX;

// x axis scale
// - number of seconds of cps_data data averaged togethor
#define DEFAULT_SECS 1

// define plot area size and position
#define MAX_Y  20
#define MAX_X  60
#define BASE_X 11

// max y axis (cpm) table
static int secs  = DEFAULT_SECS;
static int end_idx;  // will be initialized on first call to update_display

static void update_display(int maxy, int maxx)
{
    int         x, y, idx, start_idx, time_span, color;
    time_t      start_time, end_time;
    char        start_time_str[100], end_time_str[100], time_span_str[100], cpm_str[100];
    double      cpm=0;

    static char x_axis[1000];
    static bool first_call = true;

    // initialize on first call
    if (first_call) {
        end_idx = max_cps_data - 1;
        memset(x_axis, '-', MAX_X);
        first_call = false;
    }

    // if tracking is enabled then update end_idx so that the latest
    // neutron count data is displayed
    if (tracking) {
        end_idx = max_cps_data - 1;
    }

    // draw the x and y axis
    mvprintw(MAX_Y,BASE_X, x_axis);
    for (y = 0; y < MAX_Y; y++) {
        mvprintw(y,BASE_X-1, "|");
    }
    mvprintw(MAX_Y,BASE_X-1, "+");

    // draw the neutron count rate data plot
    start_idx = end_idx - secs * MAX_X;
    idx = start_idx;
    for (x = BASE_X; x < BASE_X+MAX_X; x++) {
        double cps_data = get_average_neutron_cps(idx, secs);
        if (cps_data != -1) {
            cpm = cps_data * 60;
            y = nearbyint(MAX_Y * (1 - cpm / Y_MAX));
            if (y < 0) y = 0;
            mvprintw(y,x,"*");
        }
        idx += secs;
    }

    // draw the y axis labels
    mvprintw(0, 0,       "%8d", Y_MAX);
    mvprintw(MAX_Y/2, 0, "%8d", Y_MAX/2);
    mvprintw(MAX_Y, 0,   "%8d", 0);

    // draw x axis start and end times
    start_time = cps_data_start_time + start_idx;
    end_time   = cps_data_start_time + end_idx;
    time2str(start_time, start_time_str, false);
    time2str(end_time, end_time_str, false);
    mvprintw(MAX_Y+1, BASE_X-4, "%s", start_time_str+11);
    mvprintw(MAX_Y+1, BASE_X+MAX_X-4, "%s", end_time_str+11);

    // draw x axis time span, choose units dynamically
    time_span = end_time - start_time;
    if (time_span < 60) {
        sprintf(time_span_str, "<--%d secs-->", time_span);
    } else if (time_span < 3600) {
        sprintf(time_span_str, "<--%.0f mins-->", time_span/60.);
    } else {
        sprintf(time_span_str, "<--%.3f hours-->", time_span/3600.);
    }
    mvprintw(MAX_Y+1, BASE_X+MAX_X/2-strlen(time_span_str)/2, "%s", time_span_str);

    // draw neutron cpm; color is:
    // - GREEN: displaying the current value from the detector
    // - RED: displaying old value, either from playback file, or from
    //        having moved to an old value in the live data
    sprintf(cpm_str, "%0.3f CPM", cpm);
    color = (tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
    attron(COLOR_PAIR(color));
    mvprintw(23, BASE_X+MAX_X/2-strlen(cpm_str)/2, "%s", cpm_str);
    attroff(COLOR_PAIR(color));

    // print interesting variables
    mvprintw(27, 0, "%s  %s\n", MODE_STR(mode), filename_dat);
    mvprintw(28, 0, "maxx,y=%d,%d  y_max=%d  sec=%d  max_cps_data=%d  end_idx=%d",
             maxx, maxy, Y_MAX, secs, max_cps_data, end_idx);
}

// returns averaged cps_data, or -1 if time_idx is out of range;
// the average is computed startint at time_idx, and working back for secs values
static double get_average_neutron_cps(int time_idx, int num_avg_secs)
{
    if (time_idx+1 < secs || time_idx >= max_cps_data) {
        return -1;
    }

    int i, sum = 0;
    for (i = time_idx; i > time_idx-secs; i--) {
        sum += cps_data[i];
    }

    return (double)sum / secs;
}

static int input_handler(int input_char)
{
    int _max_cps_data = max_cps_data;

    // process input_char
    switch (input_char) {
    case 4: case 'q':
        return -1; // terminates pgm
    case KEY_NPAGE: case KEY_PPAGE: {
        #define MAX_Y_MAX_TBL (sizeof(y_max_tbl)/sizeof(y_max_tbl[0]))
        if (input_char == KEY_PPAGE) y_max_tbl_idx++;
        if (input_char == KEY_NPAGE) y_max_tbl_idx--;
        clip_value(&y_max_tbl_idx, 0, MAX_Y_MAX_TBL-1);
        break; }
    case '+': case '=': case '-':
        if (input_char == '+' || input_char == '=') secs++;
        if (input_char == '-') secs--;
        clip_value(&secs, 1, 86400/MAX_X);
        break;
    case KEY_LEFT: case KEY_RIGHT: case ',': case '.': case '<': case '>': case KEY_HOME: case KEY_END:
        if (input_char == KEY_LEFT)   end_idx -= secs;
        if (input_char == KEY_RIGHT)  end_idx += secs;
        if (input_char == ',')        end_idx -= 60; 
        if (input_char == '.')        end_idx += 60; 
        if (input_char == '<')        end_idx -= 3600; 
        if (input_char == '>')        end_idx += 3600; 
        if (input_char == KEY_HOME)   end_idx  = 0;
        if (input_char == KEY_END)    end_idx  = _max_cps_data-1;
        clip_value(&end_idx, 0, _max_cps_data-1);
        tracking = (mode == MODE_LIVE && end_idx == _max_cps_data-1);
        break;
    case 'r':
        y_max_tbl_idx = DEFAULT_Y_MAX_TBL_IDX;
        secs          = DEFAULT_SECS;
        end_idx       = _max_cps_data-1;
        tracking      = (mode == MODE_LIVE && end_idx == _max_cps_data-1);
        break;
    }

    return 0;  // do not terminate pgm
}

static void clip_value(int *v, int min, int max)
{
    if (*v < min) *v = min;
    if (*v > max) *v = max;
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
    init_pair(COLOR_PAIR_GREEN, COLOR_GREEN, -1);
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

static void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char))
{
    int input_char, maxy, maxx;
    int maxy_last=0, maxx_last=0;
    int sleep_us;

    while (true) {
        // erase display
        erase();

        // get window size
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

        // if need to sleep is indicated then do so
        if (sleep_us) {
            usleep(sleep_us);
        }
    }
}

