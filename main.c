// xxx test capture mode using the simulated and the real ADC

#include <common.h>

//
// defines
//

#define MAX_DATA (100*86400)

#define MODE_CAPTURE  0
#define MODE_VIEW     1

#define MODE_STR(x) \
    ((x) == MODE_CAPTURE ? "CAPTURE" : \
     (x) == MODE_VIEW    ? "VIEW"      \
                         : "????")

//
// typedefs
//

//
// variables
//

static int    mode;
static bool   capture_tracking;  // xxx need to set this
static FILE * fp_dat;
static time_t data_start_time;

static int    data[MAX_DATA];
static int    max_data;

//
// prototypes
//

static void initialize(int argc, char **argv);
static double get_neutron_cpm(int idx, int n_avg);
static void update_display(int maxy, int maxx);
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

static void initialize(int argc, char **argv)
{
    char filename_dat[100];
    char s[100];

    #define USAGE "usage: neutron [-v <view_filename.dat] [-h]"

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
    sprintf(filename_dat, "neutron_%s.dat", time2str(time(NULL),s));
    
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
         MODE_STR(mode), filename_dat);

    // if mode is VIEW then
    //   read data from filename_dat
    // else 
    //   Create filename_dat, to save the neutron count data.
    //   Initialize the ADC, and start the ADC acquiring data from the Ludlum.
    //     Note: Init mccdaq device, is used to acquire 500000 samples per second from the
    //           Ludlum 2929 amplifier output.
    // endif
    if (mode == MODE_VIEW) {
        int line=0, t, v;

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
                if (sscanf(s, "%d %d", &t, &v) != 2) {
                    FATAL("invalid line %d in %s\n", line, filename_dat);
                }
                data[t] = v;
                max_data = t+1;
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

#if 1   // xxx temp test code
        { int i;
        for (i = 0; i < (3*86400); i++) {
            fprintf(fp_dat, "%6d %d\n", i, (i%3600)/60);
        }
        INFO("created test file %s\n", filename_dat);
        exit(0); }
#endif

        // start acquiring ADC data from mccdaq utils
        mccdaq_start(mccdaq_callback);
    }

    // stop logging to stderr
    fp_log2 = NULL;
}

char *time2str(time_t t, char *s)
{
    // example: 2021-08-21_19-21-22
    struct tm result;
    localtime_r(&t, &result);
    sprintf(s, "%4d-%02d-%02d_%02d-%02d-%02d",
            result.tm_year+1900, result.tm_mon+1, result.tm_mday, 
            result.tm_hour, result.tm_min, result.tm_sec);
    return s;
}

// -----------------  SET NEUTRON COUNT  -----------------------------------------

// called from mccdaq_cb at 1 second intervals, with neutron count for the past second

// xxx pass other stats to here, like number of restarts
void set_neutron_count(time_t time_now, int neutron_count)
{
    //INFO("idx = %d  neutron_count = %d\n", idx, neutron_count);

    // sanity check, that the time_now is 1 greater than at last call
    time_t delta_time = time_now - time_last;
    static time_t time_last;
    if (time_last != 0 && delta_time != 1) {
        WARN("unexpected delta_time %ld, should be 1\n", delta_time);
    }
    time_last = time_now;

    // determine data array idx, and sanity check
    int idx = time_now - data_start_time;
    if (idx < 0 || idx >= MAX_DATA) {
        FATAL("idx=%d out of range 0..MAX_DATA-1\n", idx);
    }

    // save neutron_count in data array
    data[idx] = neutron_count;
    max_data = idx+1;

    // xxx every 60 secs, write the data to file
    // xxx also, at exit
}

// -----------------  CURSES WRAPPER CALLBACKS  ----------------------------

// xxx more work here

#define MAX_Y 20
int max_y = 5000;
int n_avg = 5;
int end_idx = -1;

static void update_display(int maxy, int maxx)
{
    int x, y, idx, start_idx;
    double cpm;

    if (end_idx == -1) {
        end_idx = max_data - 1;
    }

    // draw the x and y axis
    // xxx make a 60 char str
    mvprintw(MAX_Y,10+1, "------------------------------------------------------------");
    for (y = 0; y < MAX_Y; y++) {
        mvprintw(y,10, "|");
    }
    mvprintw(MAX_Y,10, "+");

    mvprintw(0, 0,       "%8d", max_y);
    mvprintw(MAX_Y/2, 0, "%8d", max_y/2);
    mvprintw(MAX_Y, 0,   "%8d", 0);

    // loop over the number of display cols
    // - call get_neutron_cpm
    // - plot the data

    start_idx = end_idx - n_avg * 60;
    idx = start_idx;

    for (x = 10+1; x < (10+1+60); x++) {
        cpm = get_neutron_cpm(idx, n_avg);
        if (cpm != -1) {
            y = nearbyint(MAX_Y * (1 - cpm / max_y));
            if (y < 0) y = 0;
            mvprintw(y,x,"*");
        }
        idx += n_avg;
    }

    char start_time_str[100], end_time_str[100];
    time_t start_time, end_time;

    start_time = data_start_time + start_idx;
    end_time   = data_start_time + end_idx;
    time2str(start_time, start_time_str);
    time2str(end_time, end_time_str);

    mvprintw(MAX_Y+1, 11-4, "%s", start_time_str+11);
    mvprintw(MAX_Y+1, 11+60-4, "%s", end_time_str+11);

    char time_span_str[100];
    int time_span = end_time - start_time;
    if (time_span < 60) {
        sprintf(time_span_str, "<--%d secs-->", time_span);
    } else if (time_span < 3600) {
        sprintf(time_span_str, "<--%.1f mins-->", time_span/60.);
    } else {
        sprintf(time_span_str, "<--%.1f hours-->", time_span/3600.);
    }
    mvprintw(MAX_Y+1, 11+60/2-strlen(time_span_str)/2, "%s", time_span_str);


    char cpm_str[100];
    sprintf(cpm_str, "%s - %0.3f CPM", MODE_STR(mode), cpm);
    int color = (capture_tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
    attron(COLOR_PAIR(color));
    mvprintw(23, 11+60/2-strlen(cpm_str)/2, "%s", cpm_str);
    attroff(COLOR_PAIR(color));

    mvprintw(29, 0, "n_avg=%d   maxy=%d  maxx=%d  end_idx=%d", n_avg, maxy, maxx, end_idx);
}

// returns averaged cpm, or -1 if idx is out of range;
// the average is computed startint at idx, and working back for n_avg values
static double get_neutron_cpm(int idx, int n_avg)
{
    if (idx+1 < n_avg || idx >= max_data) {
        return -1;
    }

    int i, sum = 0;
    for (i = idx; i > idx-n_avg; i--) {
        sum += data[i];
    }

    return (double)sum / n_avg * 60;
}

static int input_handler(int input_char)
{
    // process input_char
    switch (input_char) {
    case 4: case 'q':
        // terminates pgm
        return -1;
    case 'y': case 'Y':
        if (input_char == 'Y') max_y /= 10;
        if (input_char == 'y') max_y *= 10;
        break;
    case 'x': case 'X':
        if (input_char == 'X') n_avg++;
        if (input_char == 'x') n_avg--;
        break;
    case KEY_LEFT: case KEY_RIGHT: case KEY_SLEFT: case KEY_SRIGHT: case '<': case '>':
    case KEY_HOME: case KEY_END:
        if (input_char == KEY_LEFT)   end_idx -= n_avg;
        if (input_char == KEY_RIGHT)  end_idx += n_avg;
        if (input_char == KEY_SLEFT)  end_idx -= 60; 
        if (input_char == KEY_SRIGHT) end_idx += 60; 
        if (input_char == '<')        end_idx -= 3600; 
        if (input_char == '>')        end_idx += 3600; 
        if (input_char == KEY_HOME)   end_idx  = 60 * n_avg;
        if (input_char == KEY_END)    end_idx  = max_data-1;
        break;
    case 'r':
        // xxx reset
        break;
    }

    clip_value(&max_y, 10, 10000);
    clip_value(&n_avg, 1, 86400/60);
    clip_value(&end_idx, 60*n_avg, max_data-1);

    // xxx enable tracking or disable, when in live mode and end_idx is at max_data-1

    // return 0 (means don't terminate pgm)
    return 0;
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

static void curses_runtime(void (*update_display)(int maxy, int maxx), int (*input_handler)(int input_char),
                           void (*other_handler)(void))
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

