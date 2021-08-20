#include <common.h>

static void verbose_pulse_print(int32_t pulse_start_idx, int32_t pulse_end_idx, int32_t pulse_height,
                                int32_t baseline, int16_t *data, int32_t max_data);
static void print_plot_str(int32_t value, int32_t baseline);

// -----------------  MCCDAQ CALLBACK  ----------------------------

// xxx comment
// xxx    // x (10000/2048)  ~= 50 mV

int32_t mccdaq_callback(uint16_t * d, int32_t max_d)
{
    #define MAX_DATA 1000000

    static int16_t       data[MAX_DATA];
    static int32_t       max_data;
    static int32_t       idx;
    static int32_t       baseline;
    static pulse_count_t pulse_count;
    static int32_t       total_pulses;

    #define RESET_FOR_NEXT_SEC \
        do { \
            max_data = 0; \
            idx = 0; \
            memset(&pulse_count,0,sizeof(pulse_count)); \
            total_pulses = 0; \
        } while (0)

    // if max_data too big then 
    //   print an error 
    //   reset 
    // endif
    if (max_data + max_d > MAX_DATA) {
        ERROR("max_data %d or max_d %d are too large\n", max_data, max_d);
        RESET_FOR_NEXT_SEC;
        return 0;
    }

    // copy caller supplied data to static data buffer
    memcpy(data+max_data, d, max_d*sizeof(int16_t));
    max_data += max_d;

    // if we have too little data just return, 
    // until additional data is received
    if (max_data < 100) {
        return 0;
    }

    // search for pulses in the data
    int32_t pulse_start_idx = -1;
    int32_t pulse_end_idx   = -1;
    while (true) {
        // terminate this loop when 
        // - not in-a-pulse and near the end of data OR
        // - at the end of data
        if ((pulse_start_idx == -1 && idx >= max_data-20) || 
            (idx == max_data))
        {
            break;
        }

        // print warning if data out of range
        if (data[idx] > 4095) {
            WARN("data[%d] = %u, is out of range\n", idx, data[idx]);
            data[idx] = 2048;
        }

        // update baseline ...
        // if data[idx] is close to baseline then
        //   baseline is okay
        // else if data[idx+10] is close to baseline then
        //   baseline is okay
        // else if data[idx] and the preceding 3 data values are almost the same then
        //   set baseline to data[idx]
        // endif
        if (pulse_start_idx == -1) {
            if (data[idx] >= baseline-1 && data[idx] <= baseline+1) {
                ;  // okay
            } else if (idx+10 < max_data && data[idx+10] >= baseline-1 && data[idx+10] <= baseline+1) {
                ;  // okay
            } else if ((idx >= 3) &&
                       (data[idx-1] >= data[idx]-1 && data[idx-1] <= data[idx]+1) &&
                       (data[idx-2] >= data[idx]-1 && data[idx-2] <= data[idx]+1) &&
                       (data[idx-3] >= data[idx]-1 && data[idx-3] <= data[idx]+1))
            {
                baseline = data[idx];
            }
        }

        // if baseline has not yet determined then continue
        if (baseline == 0) {
            idx++;
            continue;
        }

        // determine the pulse_start_idx and pulse_end_idx
        if (data[idx] >= (baseline + MIN_PULSE_HEIGHT) && pulse_start_idx == -1) {
            pulse_start_idx = idx;
        } else if (pulse_start_idx != -1) {
            if (data[idx] < (baseline + MIN_PULSE_HEIGHT)) {
                pulse_end_idx = idx - 1;
            } else if (idx - pulse_start_idx >= 10) {
                WARN("discarding a possible pulse because it's too long, pulse_start_idx=%d\n",
                     pulse_start_idx);
                pulse_start_idx = -1;
                pulse_end_idx = -1;
            }
        }

        // if a pulse has been located ...
        if (pulse_end_idx != -1) {
            // determine pulse_height
            int32_t i, pulse_height=-1;
            for (i = pulse_start_idx; i <= pulse_end_idx; i++) {
                if (data[i] - baseline > pulse_height) {
                    pulse_height = data[i] - baseline;
                }
            }
            assert(pulse_height >= MIN_PULSE_HEIGHT);

            // increment pulse_count histogram bucket
            int bidx = PULSE_HEIGHT_TO_BUCKET_IDX(pulse_height);
            pulse_count.bucket[bidx]++;
            total_pulses++;

            // if verbose logging is enabled and not more frequently than 
            // once per second, print this pulse to the log file
            if (verbose[1]) {
                uint64_t time_now = microsec_timer();
                static uint64_t time_last_pulse_print;
                if (time_now > time_last_pulse_print + 1000000) {
                    verbose_pulse_print(pulse_start_idx, pulse_end_idx, pulse_height, baseline, data, max_data);
                    time_last_pulse_print = time_now;
                }
            }

            // done with this pulse
            pulse_start_idx = -1;
            pulse_end_idx = -1;
        }

        // move to next data 
        idx++;
    }

    // once per second do the following ...
    uint64_t time_now = time(NULL);
    static uint64_t time_last_published;
    if (time_now > time_last_published) {    
        int32_t mccdaq_restart_count;

        // publish the pulse_count histogram for this one second interval
        publish(time_now, &pulse_count);
        time_last_published = time_now;

        // check for conditions that warrant a warning message to be logged
        mccdaq_restart_count = mccdaq_get_restart_count();
        if (mccdaq_restart_count > 1 ||
            max_data < 480000 || max_data > 520000 ||
            baseline < 2350 || baseline > 2420)
        {
            WARN("mccdaq_restart_count=%d max_data=%d baseline=%d\n",
                  mccdaq_restart_count, max_data, baseline);
        }

        // verbose logging
        VERBOSE0("ADC samples=%d restarts=%d baseline=%d total_pulses=%d\n",
                 max_data, mccdaq_restart_count, baseline, total_pulses);

        // reset variables for the next second 
        RESET_FOR_NEXT_SEC;
    }

    // return 'continue-scanning' 
    return 0;
}

// -----------------  VERBOSE PRINT PULSE  ------------------------

static void verbose_pulse_print(int32_t pulse_start_idx, int32_t pulse_end_idx, int32_t pulse_height,
                                int32_t baseline, int16_t *data, int32_t max_data)
{
    int32_t i, pulse_start_idx_extended, pulse_end_idx_extended;

    // extend the span of the pulse plot so that some baseline values are also plotted
    pulse_start_idx_extended = pulse_start_idx - 1;
    pulse_end_idx_extended = pulse_end_idx + 4;
    if (pulse_start_idx_extended < 0) {
        pulse_start_idx_extended = 0;
    }
    if (pulse_end_idx_extended >= max_data) {
        pulse_end_idx_extended = max_data-1;
    }

    // verbose print plot of the pulse
    VERBOSE1("PULSE:  height = %d   baseline = %d\n", pulse_height, baseline);
    for (i = pulse_start_idx_extended; i <= pulse_end_idx_extended; i++) {
        print_plot_str(data[i], baseline);
    }
    VERBOSE1("---------\n");
}

static void print_plot_str(int32_t value, int32_t baseline)
{
    char    str[1000];
    int32_t idx, i;

    value -= 2048;
    baseline -= 2048;

    bzero(str, sizeof(str));

    idx = value / 10;
    for (i = 0; i <= idx; i++) {
        str[i] = '*';
    }

    idx = baseline / 10;
    if (idx >= 0) {
        if (str[idx] == '*') {
            str[idx] = '+';
        } else {
            str[idx] = '|';
            for (i = 0; i < idx; i++) {
                if (str[i] == '\0') {
                    str[i] = ' ';
                }
            }
        }
    }

    VERBOSE1("%5d: %s\n", value, str);
}

