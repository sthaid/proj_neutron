#include <common.h>

//
// defines
//

//
// typedefs
//

//
// variables
//

//
// prototypes
//

static void initialize(void);
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
    // xxx test
    initialize();

    // invoke the curses user interface
    curses_init();
    curses_runtime(update_display, input_handler, NULL);
    curses_exit();

#if 0
    // if terminting due to fatal error then print the error str
    if (fatal_err_str[0] != '\0') {
        printf("%s\n", fatal_err_str);
        return 1;
    }
#endif

    // normal termination    
    return 0;
}

static void initialize(void)
{
    // xxx read file


    // init mccdaq device, used to acquire 500000 samples per second from the
    // ludlum 2929 amplifier output
    mccdaq_init();
    mccdaq_start(mccdaq_callback);
}

// -----------------  CURSES WRAPPER CALLBACKS  ----------------------------

static void update_display(int maxy, int maxx)
{
    static int cnt;

    attron(COLOR_PAIR(COLOR_PAIR_CYAN));
    cnt++;
    mvprintw(0,0, "HELLO %d\n", cnt);
    attroff(COLOR_PAIR(COLOR_PAIR_CYAN));
}

static int input_handler(int input_char)
{
    // process input_char
    if (input_char == 4) {  // 4 is ^d
        return -1;  // terminates pgm
    }

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

