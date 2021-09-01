#include <common.h>

//
// defines
//

#define MAX_DATA (20*86400)

#define MODE_LIVE      0
#define MODE_PLAYBACK  1
#define MODE_STR(x)    ((x) == MODE_LIVE ? "LIVE" : "PLAYBACK")

#define DISPLAY_PLOT      0
#define DISPLAY_HISTOGRAM 1

#define DEFAULT_AVG_INTVL 5  
#define DEFAULT_PHT       40    // PHT = Pulse Height Threshold
#define DEFAULT_Y_MAX     1000  // must be an entry in y_max_tbl

#define FILE_MAGIC 0x77777777

//
// typedefs
//

typedef struct {
    int magic;
    int pad;
    uint64_t data_start_time;
} file_hdr_t;

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
static char           filename[200];
static int            fd=-1;
static pthread_t      live_mode_write_data_thread_id;

// params ...
static int avg_intvl = DEFAULT_AVG_INTVL;
static int pht       = DEFAULT_PHT;
static int y_max     = DEFAULT_Y_MAX;

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
static char *time_duration_str(int time_span);
static double *get_average_cpm_for_all_buckets(int time_idx);
static double get_average_cpm_for_pht(int time_idx);
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

    // set mode and filename to default
    char s[100];
    mode = MODE_LIVE;
    sprintf(filename, "neutron_%s.dat", time2str(time(NULL),s,true));

    // parse options
    while (true) {
        int ch = getopt(argc, argv, "p:v:h");
        if (ch == -1) {
            break;
        }
        switch (ch) {
        case 'p':
            mode = MODE_PLAYBACK;
            strcpy(filename, optarg);
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
    INFO("-------- STARTING: MODE=%s FILENAME=%s --------\n", MODE_STR(mode), filename);

    // register signal handler, used to terminate program gracefully
    static struct sigaction act;
    act.sa_handler = sig_hndlr;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    // init the param values (pht, avg_intvl, y_max) from the neutron.params file;
    // note that this file will not exist until written using the 'w' cmd
    read_neutron_params();

    // if mode is PLAYBACK then
    //   read data from filename
    // else 
    //   Create filename, to save the neutron count data.
    //   Initialize the ADC, and start the ADC acquiring data from the Ludlum.
    //     Note: Init mccdaq device, is used to acquire 500000 samples per second from the
    //           Ludlum 2929 amplifier output.
    // endif
    if (mode == MODE_PLAYBACK) {
        int rc, data_len;
        file_hdr_t file_hdr;
        struct stat buf;
        char s[100];

        // PLAYBACK mode init ...
        
        // open filename
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            FATAL("%s, open for reading, %s\n", filename, strerror(errno));
        }

        // read and verify file_hdr
        rc = read(fd, &file_hdr, sizeof(file_hdr));
        if (rc != sizeof(file_hdr)) {
            FATAL("%s, read file_hdr, rc=%d, %s\n", filename, rc, strerror(errno));
        }
        if (file_hdr.magic != FILE_MAGIC) {
            FATAL("%s, invalid file_hdr, 0x%x\n", filename, file_hdr.magic);
        }

        // the file data following the file_hdr is an array of pulse_count_t;
        // determine the data_len
        rc = fstat(fd, &buf);
        if (rc < 0) {
            FATAL("%s, failed fstat, %s\n", filename, strerror(errno));
        }
        data_len = buf.st_size - sizeof(file_hdr);
        if (data_len <= 0 || data_len >= sizeof(data)) {
            FATAL("%s, data_len out of range, data_len=%d\n", filename, data_len);
        }
        if ((data_len % sizeof(pulse_count_t)) != 0) {
            FATAL("%s, data_len=%d is not multiple of %zd\n", filename, data_len, sizeof(pulse_count_t));
        }

        // read the data
        rc = read(fd, data, data_len);
        if (rc != data_len) {
            FATAL("%s, read data, rc=%d, %s\n", filename, rc, strerror(errno));
        }

        // close file
        close(fd);
        fd = -1;

        // set global variables: data_start_time and max_data
        data_start_time = file_hdr.data_start_time;
        max_data = data_len / sizeof(pulse_count_t);
        INFO("data_start_time = %ld, %s\n", data_start_time, time2str(data_start_time,s,false));
        INFO("max_data        = %d\n", max_data);
    } else {
        file_hdr_t file_hdr;
        int rc;

        // LIVE mode init ...

        // init mccdaq utils
        rc = mccdaq_init();
        if (rc < 0) {
            FATAL("mccdaq_init failed\n");
        }

        // create filename for writing, and write the file_hdr
        fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0644);
        if (fd < 0) {
            FATAL("%s, open for writing, %s\n", filename, strerror(errno));
        }
        memset(&file_hdr, 0, sizeof(file_hdr));
        file_hdr.magic = FILE_MAGIC;
        file_hdr.data_start_time = time(NULL);
        rc = write(fd, &file_hdr, sizeof(file_hdr));
        if (rc != sizeof(file_hdr)) {
            FATAL("%s, write file_hdr, rc=%d, %s\n", filename, rc, strerror(errno));
        }

        // set global variables: data_start_time and max_data
        data_start_time = file_hdr.data_start_time;
        max_data = 0;
        INFO("data_start_time = %ld, %s\n", data_start_time, time2str(data_start_time,s,false));
        INFO("max_data        = %d\n", max_data);

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

    fp = fopen("neutron.params", "r");
    if (fp == NULL) {
        WARN("failed to open neutron.params for reading\n");
        return;
    }
    fgets(s, sizeof(s), fp);
    fclose(fp);

    cnt = sscanf(s, "pht=%d avg_intvl=%d y_max=%d\n", &pht, &avg_intvl, &y_max);
    if (cnt != 3) {
        ERROR("contents of neutron.params is invalid\n");
    }

    INFO("read neutron.params: pht=%d avg_intvl=%d y_max=%d\n", pht, avg_intvl, y_max);
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
    fprintf(fp, "pht=%d avg_intvl=%d y_max=%d\n", pht, avg_intvl, y_max);
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
    int delta_time = time_now - time_last;
    if (time_last != 0 && delta_time != 1) {
        WARN("unexpected delta_time %d, should be 1\n", delta_time);
    }
    time_last = time_now;

    // save neutron_count in data array
    data[time_idx] = *pc;
    __sync_synchronize();
    max_data = time_idx+1;
}

static void * live_mode_write_data_thread(void *cx)
{
    int        time_idx, rc;
    bool       terminate;
    static int last_time_idx_written = -1;

    // file should already been opened in initialize()
    assert(fd > 0);

    // loop, writing data to neutron.dat file
    while (true) {
        // read program_terminating flag prior to writing to the file
        terminate = program_terminating;

        // write new neutron count data entries to the file
        for (time_idx = last_time_idx_written+1; time_idx < max_data; time_idx++) {
            pulse_count_t *pc = &data[time_idx];

            rc = write(fd, pc, sizeof(pulse_count_t));
            if (rc != sizeof(pulse_count_t)) {
                ERROR("writing pulse_count to %s, rc=%d, %s\n", filename, rc, strerror(errno));
            }

            last_time_idx_written = time_idx;
        }

        // if terminate has been requested then break
        if (terminate) {
            break;
        }

        // sleep 1 sec
        sleep(1);
    }

    // close the file, and exit this thread
    close(fd);
    fd = -1;
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

    int y;

    // initialize on first call
    if (first_call) {
        INFO("maxx = %d  maxy = %d\n", maxx, maxy);
        end_idx = max_data - 1;
        memset(x_axis, '-', MAX_X);
        first_call = false;
    }

    // draw the x and y axis
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

    // print params, mode, filename, and max_data
    mvprintw(24, 0, "pht       = %d", pht);
    mvprintw(25, 0, "avg_intvl = %d", avg_intvl);
    mvprintw(26, 0, "y_max     = %d", y_max);
    print_centered(27, 40, COLOR_PAIR_NONE, "%s", MODE_STR(mode));
    print_centered(28, 40, COLOR_PAIR_NONE, "%s - %d", filename, max_data);

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
    // - GREEN: displaying the current value from the detector, in LIVE mode
    // - RED: displaying old value, either from playback file, or from
    //        having moved to an old value in the LIVE mode data
    double cpm = get_average_cpm_for_pht(end_idx);
    if (cpm != -1) {
        int color = (tracking ? COLOR_PAIR_GREEN : COLOR_PAIR_RED);
        print_centered(24, 40, color, "%0.3f CPM", cpm);
    }
}

static void update_display_plot(void)
{
    int    x, y, idx, start_idx;
    time_t start_time, end_time;
    char   start_time_str[100], end_time_str[100];

    // if tracking is enabled then update end_idx so that the latest
    // neutron count data is displayed; note that tracking can only be
    // set when in LIVE mode
    if (tracking) {
        end_idx = max_data - 1;
    }

    // draw the neutron count rate plot
    start_idx = end_idx - avg_intvl * (MAX_X - 1);
    idx = start_idx;
    for (x = BASE_X; x < BASE_X+MAX_X; x++) {
        double cpm = get_average_cpm_for_pht(idx);
        if (cpm != -1) {
            y = nearbyint(MAX_Y * (1 - cpm / y_max));
            if (y < 0) y = 0;
            mvprintw(y,x,"*");
        }
        idx += avg_intvl;
    }

    // draw x axis start and end times
    start_time = data_start_time + start_idx - avg_intvl;
    end_time   = data_start_time + end_idx;
    time2str(start_time, start_time_str, false);
    time2str(end_time, end_time_str, false);
    mvprintw(MAX_Y+1, BASE_X-4, "%s", start_time_str+11);
    mvprintw(MAX_Y+1, BASE_X+MAX_X-4, "%s", end_time_str+11);
    start_time_str[10] = '\0';
    end_time_str[10] = '\0';
    mvprintw(MAX_Y+2, BASE_X-4, "%s", start_time_str);
    mvprintw(MAX_Y+2, BASE_X+MAX_X-6, "%s", end_time_str);

    // draw x axis time span
    print_centered(MAX_Y+1, 40, COLOR_PAIR_NONE, "<- %s ->", 
                   time_duration_str(end_time - start_time));
}

static void update_display_histogram(void)
{
    int       bidx, x, y, yy;
    double  * cpm;
    time_t    start_time, end_time;
    char      start_time_str[100], end_time_str[100];
    const int first_bucket = PULSE_HEIGHT_TO_BUCKET_IDX(MIN_PULSE_HEIGHT);

    // calculate the array of average bucket values; where each average bucket
    // value returned is the average over the interval end_idx-avg_intvl+1 to end_idx
    cpm = get_average_cpm_for_all_buckets(end_idx);

    // loop over all buckets and display the histogram values for each bucket
    for (bidx = first_bucket; bidx < MAX_BUCKET; bidx++) {
        // if no cpm value available at this bucket idx then continue
        if (cpm[bidx] == -1) {
            continue;
        }

        // determine the x,y coordinates for plotting this histogram bucket
        x = BASE_X + bidx;
        y = nearbyint(MAX_Y * (1 - cpm[bidx] / y_max));
        if (y < 0) y = 0;

        // plot the histogram bucket; use color CYAN if this bucket 
        // falls within the pulse height threshold
        if (bidx >= PULSE_HEIGHT_TO_BUCKET_IDX(pht)) attron(COLOR_PAIR(COLOR_PAIR_CYAN));
        for (yy = y; yy <= MAX_Y; yy++) {
            mvprintw(yy,x,"*");
        }
        if (bidx >= PULSE_HEIGHT_TO_BUCKET_IDX(pht)) attroff(COLOR_PAIR(COLOR_PAIR_CYAN));
    }

    // label the x axis
    for (bidx = first_bucket; bidx < MAX_BUCKET; bidx++) {
        x = BASE_X + bidx;
        if (bidx == first_bucket || bidx == MAX_BUCKET/2 || bidx == MAX_BUCKET-1) {
            print_centered(MAX_Y+1, x, COLOR_PAIR_NONE, "%d", BUCKET_IDX_TO_PULSE_HEIGHT(bidx));
        }
    }

    // display the time range over which this histogram has been evaluated
    end_time   = data_start_time + end_idx;
    start_time = end_time - avg_intvl;
    time2str(start_time, start_time_str, false);
    time2str(end_time, end_time_str, false);
    print_centered(MAX_Y+2, 40, COLOR_PAIR_NONE, "<- %s ... %s ->",
                   start_time_str+11, end_time_str+11);
    print_centered(MAX_Y+3, 40, COLOR_PAIR_NONE, "%s", 
                  time_duration_str(end_time - start_time));
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

static char *time_duration_str(int time_span)
{
    static char s[200];

    if (time_span < 60) {
        sprintf(s, "%d secs", time_span);
    } else if (time_span < 3600) {
        sprintf(s, "%.0f mins", time_span/60.);
    } else {
        sprintf(s, "%.3f hours", time_span/3600.);
    }

    return s;
}

// Return cpm value array that is the average for all buckets
//  over the time range time_idx-avg_intvl+1 to time_idx;
// Return array of -1 if time_idx is not valid.
static double *get_average_cpm_for_all_buckets(int time_idx)
{
    #define MAX_SAVE 2048

    static bool first_call = true;
    static double cpm_no_data[MAX_BUCKET];
    static struct save_s {
        int time_idx;
        int avg_intvl;
        double cpm[MAX_BUCKET];
    } save[MAX_SAVE];

    struct save_s *s;
    int tidx, bidx, hidx, sum;

    // on first call init cpm_no_data, which is the return value 
    // for when time_idx is out of range
    if (first_call) {
        for (bidx = 0; bidx < MAX_BUCKET; bidx++) {
            cpm_no_data[bidx] = -1;
        }
        first_call = false;
    }

    // if time_idx is out of range then return cpm_no_data
    if (time_idx-avg_intvl+1 < 0 || time_idx >= max_data) {
        return cpm_no_data;
    }

    // do hash tbl lookup to see if a saved result is available; 
    // if so, then return the saved result
    hidx = (time_idx % MAX_SAVE);
    s = &save[hidx];
    if (s->time_idx == time_idx && s->avg_intvl == avg_intvl) {
        return s->cpm;
    }

    // calculate the average for each bucket over the range
    // time_idx-avg_intvl+1 to time_idx
    for (bidx = 0; bidx < MAX_BUCKET; bidx++) {
        sum = 0;
        for (tidx = time_idx-avg_intvl+1; tidx <= time_idx; tidx++) {
            sum += data[tidx].bucket[bidx];
        }
        s->cpm[bidx] = ((double)sum / avg_intvl) * 60;
    }

    // set the time_idx and avg_intvl signature in the result save tbl
    // update table of saved results with the 
    s->time_idx = time_idx;
    s->avg_intvl = avg_intvl;

    // return the array of bucket average values
    return s->cpm;
}

// Return cpm value that is the average for the sum of all buckets >= pht,
//  over the time range time_idx-avg_intvl+1 to time_idx;
// Return -1 if time_idx is not valid.
static double get_average_cpm_for_pht(int time_idx)
{
    int tidx, sum, sum_buckets, bidx;
    double cpm;

    static struct {
        int    avg_intvl;
        int    pht;
        double cpm[MAX_DATA];
        bool   cpm_valid[MAX_DATA];
    } save;

    // if time_idx is out of range then return -1
    if (time_idx-avg_intvl+1 < 0 || time_idx >= max_data) {
        return -1;
    }

    // if current avg_intvl or pht is different than the avg_intvl/pht associated
    // with the saved results, then clear the saved results so that they
    // will be recalculated
    if (save.avg_intvl != avg_intvl || save.pht != pht) {
        memset(save.cpm_valid, 0, sizeof(save.cpm_valid));
        save.avg_intvl = avg_intvl;
        save.pht = pht;
    }

    // if there is a saved result available for time_idx then return it
    if (save.cpm_valid[time_idx]) {
        return save.cpm[time_idx];
    }

    // calculate the cpm value that will be returned; and save the result 
    sum = 0;
    for (tidx = time_idx-avg_intvl+1; tidx <= time_idx; tidx++) {
        // sum the buckets which contain counts for pules with heights
        // that are greater or eqal to the pulse haight threshold
        sum_buckets = 0;
        for (bidx = PULSE_HEIGHT_TO_BUCKET_IDX(pht); bidx < MAX_BUCKET; bidx++) {
            sum_buckets += data[tidx].bucket[bidx];
        }

        // sum the sum_buckets that was just calculated
        sum += sum_buckets;
    }
    cpm = ((double)sum / avg_intvl) * 60;
    save.cpm[time_idx] = cpm;
    save.cpm_valid[time_idx] = true;

    // return the cpm value
    return cpm;
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
        static int y_max_tbl[] = { 2, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000, };  // cpm
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
        // adjust avg_intvl
        int incr = (avg_intvl >= 100 ? 10 : 1);
        if (input_char == '-') avg_intvl -= incr;
        if (input_char == '=') avg_intvl += incr;
        clip_value(&avg_intvl, 1, 3600);
        break; }
    case '1': case '2':
        // adjust pht (pulse height threshold)
        if (input_char == '1') pht -= BUCKET_SIZE;
        if (input_char == '2') pht += BUCKET_SIZE;
        clip_value(&pht, MIN_PULSE_HEIGHT, MAX_PULSE_HEIGHT);
        break;
    case KEY_LEFT: case KEY_RIGHT:   // end_idx adjusted by:  avg_intvl
    case ',': case '.':              // end_idx adjusted by:  1 second
    case '<': case '>':              // end_idx adjusted by:  60 secs
    case 'k': case 'l':              // end_idx adjusted by:  3600 secs
    case KEY_HOME:                   // end_idx = start-of-data
    case KEY_END:                    // end_idx = end-of-data
        // adjust end_idx
        if (input_char == KEY_LEFT)   end_idx -= avg_intvl;
        if (input_char == KEY_RIGHT)  end_idx += avg_intvl;
        if (input_char == ',')        end_idx -= 1;
        if (input_char == '.')        end_idx += 1; 
        if (input_char == '<')        end_idx -= 60; 
        if (input_char == '>')        end_idx += 60; 
        if (input_char == 'k')        end_idx -= 3600; 
        if (input_char == 'l')        end_idx += 3600; 
        if (input_char == KEY_HOME)   end_idx  = 0;
        if (input_char == KEY_END)    end_idx  = _max_data-1;
        clip_value(&end_idx, 0, _max_data-1);
        tracking = (mode == MODE_LIVE && end_idx == _max_data-1);
        break;
    case KEY_F0+1: case KEY_F0+2: 
        // select plot or historgram display
        display_select = (input_char == KEY_F0+1 ? DISPLAY_PLOT : DISPLAY_HISTOGRAM);
        break;
    case 's': case 'r':
        // save or recall parameters
        if (input_char == 'r') {
            read_neutron_params();
        } else {
            write_neutron_params();
        }
        break;
    case 'R':
        // reset
        y_max     = DEFAULT_Y_MAX;
        avg_intvl = DEFAULT_AVG_INTVL;
        pht       = DEFAULT_PHT;
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

