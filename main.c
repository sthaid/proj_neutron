// xxx todo ...
// - testing
//
// xxx maybe todo ...
// - save and restore params

// xxx change SECS to AVG_INTVL

#include <common.h>

//
// defines
//

#define MAX_DATA (30*86400)

#define MODE_LIVE      0
#define MODE_PLAYBACK  1
#define MODE_STR(x)    ((x) == MODE_LIVE ? "LIVE" : "PLAYBACK")

#define DISPLAY_PLOT      0
#define DISPLAY_HISTOGRAM 1

#define DEFAULT_SECS      5  
#define DEFAULT_PHT       30    // PHT = Pulse Height Threshold
#define DEFAULT_Y_MAX     1000  // must be an entry in y_max_tbl

//
// typedefs
//


//
// variables
//

// variables ...
static int            mode;
static bool           tracking;
static int            display_select;
static int            end_idx;
static bool           program_terminating;

// neutron pulse count data ...
static time_t         data_start_time;
static pulse_count_t  data[MAX_DATA];
static int            max_data;

// save neutron pulse count data to file ...
static char           filename_dat[200];
static FILE         * fp_dat;
static pthread_t      live_mode_write_data_thread_id;

// params ...
static int secs   = DEFAULT_SECS;
static int pht    = DEFAULT_PHT;
static int y_max  = DEFAULT_Y_MAX;

//
// prototypes
//

static void initialize(int argc, char **argv);
static void sig_hndlr(int sig);
static void clip_value(int *v, int min, int max);
static void read_neutron_params(void);
static void write_neutron_params(void);

static void * live_mode_write_data_thread(void *cx);
static void update_display(int maxy, int maxx);
static void update_display_plot(void);
static void update_display_histogram(void);
static void print_centered(int y, int ctrx, int color, char *fmt, ...) __attribute__((format(printf, 4, 5)));
static double get_average_cpm(int time_idx, int first_bucket, int last_bucket);
static int input_handler(int input_char);

//
// curses wrapper definitions
//

#define COLOR_PAIR_NONE  0
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

// -----------------  MAIN & INITIALIZE & UTILS  ---------------------------------

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
        assert(live_mode_write_data_thread_id != 0);
        pthread_join(live_mode_write_data_thread_id, NULL);
    }
    return 0;
}

static void initialize(int argc, char **argv)
{
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
    char s[100];
    mode = MODE_LIVE;
    sprintf(filename_dat, "neutron_%s.dat", time2str(time(NULL),s,true));
    
    // parse options
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
    INFO("-------- STARTING: MODE=%s FILENAME=%s --------\n", MODE_STR(mode), filename_dat);

    // register signal handler, used to terminate program gracefully
    static struct sigaction act;
    act.sa_handler = sig_hndlr;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    // init the param values (pht, secs, y_max) from the neutron.params file;
    // note that this file will not exist until written using the 'w' cmd
    read_neutron_params();

    // if mode is PLAYBACK then
    //   read data from filename_dat
    // else 
    //   Create filename_dat, to save the neutron count data.
    //   Initialize the ADC, and start the ADC acquiring data from the Ludlum.
    //     Note: Init mccdaq device, is used to acquire 500000 samples per second from the
    //           Ludlum 2929 amplifier output.
    // endif
    if (mode == MODE_PLAYBACK) {
        int line=0, time_idx, cnt;
        pulse_count_t pc;
        char s[1000];

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
                cnt = sscanf(s, "%d - "
                                "%d %d %d %d %d %d %d %d %d %d "
                                "%d %d %d %d %d %d %d %d %d %d "
                                "%d %d %d %d %d %d %d %d %d %d ",
                             &time_idx,
                             &pc.bucket[0], &pc.bucket[1], &pc.bucket[2], &pc.bucket[3], &pc.bucket[4],
                             &pc.bucket[5], &pc.bucket[6], &pc.bucket[7], &pc.bucket[8], &pc.bucket[9],
                             &pc.bucket[10], &pc.bucket[11], &pc.bucket[12], &pc.bucket[13], &pc.bucket[14],
                             &pc.bucket[15], &pc.bucket[16], &pc.bucket[17], &pc.bucket[18], &pc.bucket[19],
                             &pc.bucket[20], &pc.bucket[21], &pc.bucket[22], &pc.bucket[23], &pc.bucket[24],
                             &pc.bucket[25], &pc.bucket[26], &pc.bucket[27], &pc.bucket[28], &pc.bucket[29]);
                if (cnt != MAX_BUCKET+1) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
                if (time_idx < 0 || time_idx >= MAX_DATA) {
                    FATAL("time_idx %d out of range\n", time_idx);
                }
                data[time_idx] = pc;
                max_data = time_idx+1;
            }
        }
        fclose(fp_dat);
        fp_dat = NULL;
        INFO("max_data = %d\n", max_data);
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
        data_start_time = time(NULL);
        fprintf(fp_dat, "# data_start_time = %ld\n", data_start_time);

        // create thread that will write the neutron count data to
        // the neutron.dat file
        pthread_create(&live_mode_write_data_thread_id, NULL, 
                       live_mode_write_data_thread, NULL);

        // start acquiring ADC data using mccdaq utils
        mccdaq_start(mccdaq_callback);

        // enable tracking, this applies only to MODE_LIVE, and causes
        // update_display to display the latest neutron count data
        tracking = true;
    }

    // stop logging to stderr
    fp_log2 = NULL;
}

static void sig_hndlr(int sig)
{
    curses_term_req = true;
}

static void clip_value(int *v, int min, int max)
{
    if (*v < min) *v = min;
    if (*v > max) *v = max;
}

static void read_neutron_params(void)
{
    FILE *fp;
    char s[100] = {0};
    int cnt;

    INFO("reading neutron.params\n");

    fp = fopen("neutron.params", "r");
    if (fp == NULL) {
        WARN("failed to open neutron.params for reading\n");
        return;
    }
    fgets(s, sizeof(s), fp);
    fclose(fp);

    cnt = sscanf(s, "pht=%d secs=%d y_max=%d\n", &pht, &secs, &y_max);
    if (cnt != 3) {
        ERROR("contents of neutron.params is invalid\n");
    }
}

static void write_neutron_params(void)
{
    FILE *fp;

    INFO("writing neutron.params\n");

    fp = fopen("neutron.params", "w");
    if (fp == NULL) {
        ERROR("failed to open neutron.params for writing\n");
        return;
    }
    fprintf(fp, "pht=%d secs=%d y_max=%d\n", pht, secs, y_max);
    fclose(fp);
}

// -----------------  LIVE MODE ROUTINES  ----------------------------------------

// called from mccdaq_cb at 1 second intervals, with pulse count histogram data 
// for the past second
void publish(time_t time_now, pulse_count_t *pc)
{
    // determine data array time_idx, and sanity check
    int time_idx = time_now - data_start_time;
    if (time_idx < 0 || time_idx >= MAX_DATA) {
        FATAL("time_idx=%d out of range 0..MAX_DATA-1\n", time_idx);
    }

    // sanity check, that the time_now is 1 greater than at last call
    static time_t time_last;
    time_t delta_time = time_now - time_last;
    if (time_last != 0 && delta_time != 1) {
        WARN("unexpected delta_time %ld, should be 1\n", delta_time);
    }
    time_last = time_now;

    // save neutron_count in data array
    data[time_idx] = *pc;
    __sync_synchronize();
    max_data = time_idx+1;
}

static void * live_mode_write_data_thread(void *cx)
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
        for (time_idx = last_time_idx_written+1; time_idx < max_data; time_idx++) {
            pulse_count_t *pc = &data[time_idx];
            fprintf(fp_dat, "%d - "
                             "%d %d %d %d %d %d %d %d %d %d "
                             "%d %d %d %d %d %d %d %d %d %d "
                             "%d %d %d %d %d %d %d %d %d %d "
                             "\n",
                    time_idx,
                    pc->bucket[0], pc->bucket[1], pc->bucket[2], pc->bucket[3], pc->bucket[4],
                    pc->bucket[5], pc->bucket[6], pc->bucket[7], pc->bucket[8], pc->bucket[9],
                    pc->bucket[10], pc->bucket[11], pc->bucket[12], pc->bucket[13], pc->bucket[14],
                    pc->bucket[15], pc->bucket[16], pc->bucket[17], pc->bucket[18], pc->bucket[19],
                    pc->bucket[20], pc->bucket[21], pc->bucket[22], pc->bucket[23], pc->bucket[24],
                    pc->bucket[25], pc->bucket[26], pc->bucket[27], pc->bucket[28], pc->bucket[29]);
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

// define plot area size and position
#define MAX_Y  20
#define MAX_X  60
#define BASE_X 11

static void update_display(int maxy, int maxx)
{
    static char x_axis[1000];
    static bool first_call = true;

    // initialize on first call
    if (first_call) {
        INFO("maxx = %d  maxy = %d\n", maxx, maxy);
        end_idx = max_data - 1;
        memset(x_axis, '-', MAX_X);
        first_call = false;
    }

    // if tracking is enabled then update end_idx so that the latest
    // neutron count data is displayed
    if (tracking) {
        end_idx = max_data - 1;
    }

    // draw the x and y axis
    int y;
    mvprintw(MAX_Y,BASE_X, x_axis);
    for (y = 0; y < MAX_Y; y++) {
        mvprintw(y,BASE_X-1, "|");
    }
    mvprintw(MAX_Y,BASE_X-1, "+");

    // draw the y axis labels
    mvprintw(0, 0,       "%8d", y_max);
    mvprintw(MAX_Y/2, 0, "%8d", y_max/2);
    mvprintw(MAX_Y, 0,   "%8d", 0);
    mvprintw(5, 3,       "CPM");

    // display either the neutron count plot or histogram
    switch (display_select) {
    case DISPLAY_PLOT:
        update_display_plot();
        break;
    case DISPLAY_HISTOGRAM:
        update_display_histogram();
        break;
    }

    // draw neutron cpm; color is:
    // - GREEN: displaying the current value from the detector
    // - RED: displaying old value, either from playback file, or from
    //        having moved to an old value in the live data
    double cpm = get_average_cpm(end_idx, PULSE_HEIGHT_TO_BUCKET_IDX(pht), MAX_BUCKET-1);
    if (cpm != -1) {
        int color = (tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
        print_centered(24, 40, color, "%0.3f CPM", cpm);
    }

    // print params, mode, filename_dat, and max_data
    mvprintw(24, 0, "pht   = %d", pht);
    mvprintw(25, 0, "secs  = %d", secs);
    mvprintw(26, 0, "y_max = %d", y_max);
    print_centered(27, 40, COLOR_PAIR_NONE, "%s", MODE_STR(mode));
    print_centered(28, 40, COLOR_PAIR_NONE, "%s - %d", filename_dat, max_data);
}

static void update_display_plot(void)
{
    int    x, y, idx, start_idx, time_span;
    time_t start_time, end_time;
    char   start_time_str[100], end_time_str[100], time_span_str[100];

    // draw the neutron count rate plot
    start_idx = end_idx - secs * (MAX_X - 1);
    idx = start_idx;
    for (x = BASE_X; x < BASE_X+MAX_X; x++) {
        double cpm = get_average_cpm(idx, PULSE_HEIGHT_TO_BUCKET_IDX(pht), MAX_BUCKET-1);
        if (cpm != -1) {
            y = nearbyint(MAX_Y * (1 - cpm / y_max));
            if (y < 0) y = 0;
            mvprintw(y,x,"*");
        }
        idx += secs;
    }

    // draw x axis start and end times
    start_time = data_start_time + start_idx - secs;
    end_time   = data_start_time + end_idx;
    time2str(start_time, start_time_str, false);
    time2str(end_time, end_time_str, false);
    mvprintw(MAX_Y+1, BASE_X-4, "%s", start_time_str+11);
    mvprintw(MAX_Y+1, BASE_X+MAX_X-4, "%s", end_time_str+11);
    start_time_str[10] = '\0';
    end_time_str[10] = '\0';
    mvprintw(MAX_Y+2, BASE_X-4, "%s", start_time_str);
    mvprintw(MAX_Y+2, BASE_X+MAX_X-6, "%s", end_time_str);

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
}

static void update_display_histogram(void)
{
    int i, x, y, yy, color;
    double cpm;

    for (i = 2; i < MAX_BUCKET; i++) {  // xxx '2'
        cpm = get_average_cpm(end_idx, i, i);
        if (cpm == -1) {
            continue;
        }

        x = BASE_X + 2 * i;
        y = nearbyint(MAX_Y * (1 - cpm / y_max));
        if (y < 0) y = 0;

        if (i == 2 || i == MAX_BUCKET/2 || i == MAX_BUCKET-1) {
            print_centered(MAX_Y+1, x, COLOR_PAIR_NONE, "%d", BUCKET_IDX_TO_PULSE_HEIGHT(i));
        }

        if (i >= PULSE_HEIGHT_TO_BUCKET_IDX(pht)) {
            color = (tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
            attron(COLOR_PAIR(color));
        }
        for (yy = y; yy <= MAX_Y; yy++) {
            mvprintw(yy,x,"*");
        }
        if (i >= PULSE_HEIGHT_TO_BUCKET_IDX(pht)) {
            color = (tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
            attroff(COLOR_PAIR(color));
        }
    }
}

static void print_centered(int y, int ctrx, int color, char *fmt, ...)
{
    va_list ap;
    int len;
    char str[200];

    va_start(ap, fmt);
    vsprintf(str, fmt, ap);
    va_end(ap);
    len = strlen(str);

    if (color != COLOR_PAIR_NONE) attron(COLOR_PAIR(color));
    mvprintw(y, ctrx-len/2, "%s", str);
    if (color != COLOR_PAIR_NONE) attroff(COLOR_PAIR(color));
}

// Returns a cpm value that is the average cpm over the range time_idx-secs+1 ... time_idx.
// For each time in this range the cpm for that time is computed by summing over the
//  specified range of histogram buckets.
// If time_idx is out of range, then -1 is returned.
static double get_average_cpm(int time_idx, int first_bucket, int last_bucket)
{
    int i, j, sum=0;

    assert(first_bucket >= 0 && first_bucket < MAX_BUCKET);
    assert(last_bucket >= 0 && last_bucket < MAX_BUCKET);
    assert(last_bucket >= first_bucket);

    if (time_idx-secs+1 < 0 || time_idx >= max_data) {
        return -1;
    }

    for (i = time_idx-secs+1; i <= time_idx; i++) {
        for (j = first_bucket; j <= last_bucket; j++) {
            sum += data[i].bucket[j];
        }
    }

    return ((double)sum / secs) * 60;
}

static int input_handler(int input_char)
{
    int _max_data = max_data;

    // process input_char
    switch (input_char) {
    case 4: case 'q':
        // terminate program on ^d or q
        return -1;
    case KEY_NPAGE: case KEY_PPAGE: {
        // adjust y_max
        #define MAX_Y_MAX_TBL (sizeof(y_max_tbl)/sizeof(y_max_tbl[0]))
        static int y_max_tbl[] = { 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000, };  // cpm
        int i;
        for (i = 0; i < MAX_Y_MAX_TBL; i++) {
            if (y_max == y_max_tbl[i]) {
                break;
            }
        }
        assert(i < MAX_Y_MAX_TBL);
        if (input_char == KEY_PPAGE) i++;
        if (input_char == KEY_NPAGE) i--;
        clip_value(&i, 0, MAX_Y_MAX_TBL-1);
        y_max = y_max_tbl[i];
        break; }
    case '-': case '=': {
        // adjust secs
        int incr = (secs >= 100 ? 10 : 1);
        if (input_char == '-') secs -= incr;
        if (input_char == '=') secs += incr;
        clip_value(&secs, 1, 3600);
        break; }
    case '1': case '2':
        // adjust pht (pulse height threshold)
        if (input_char == '1') pht -= BUCKET_SIZE;
        if (input_char == '2') pht += BUCKET_SIZE;
        clip_value(&pht, MIN_PULSE_HEIGHT, MAX_PULSE_HEIGHT);
        break;
    case KEY_LEFT: case KEY_RIGHT: case ',': case '.': case '<': case '>': case KEY_HOME: case KEY_END:
        // adjust end_idx
        if (input_char == KEY_LEFT)   end_idx -= secs;
        if (input_char == KEY_RIGHT)  end_idx += secs;
        if (input_char == ',')        end_idx -= 60;   // xxx also need single secs
        if (input_char == '.')        end_idx += 60; 
        if (input_char == '<')        end_idx -= 3600; 
        if (input_char == '>')        end_idx += 3600; 
        if (input_char == KEY_HOME)   end_idx  = 0;
        if (input_char == KEY_END)    end_idx  = _max_data-1;
        clip_value(&end_idx, 0, _max_data-1);
        tracking = (mode == MODE_LIVE && end_idx == _max_data-1);
        break;
    case KEY_F0+1: case KEY_F0+2: 
        // select plot or historgram display
        display_select = (input_char == KEY_F0+1 ? DISPLAY_PLOT : DISPLAY_HISTOGRAM);
        break;
    case 'w': case 'r':
        // read or wrie parameters
        if (input_char == 'r') {
            read_neutron_params();
        } else {
            write_neutron_params();
        }
        break;
    case 'R':
        // reset
        y_max    = DEFAULT_Y_MAX;
        secs     = DEFAULT_SECS;
        pht      = DEFAULT_PHT;
        write_neutron_params();

        end_idx  = _max_data-1;
        tracking = (mode == MODE_LIVE && end_idx == _max_data-1);
        break;
    }

    return 0;  // do not terminate pgm
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

