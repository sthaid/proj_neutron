====================================
INTRODUCTION
====================================

I am a fusor hobbyist, and I have used information provided on fusor.net 
to make a working fusor in my basement. To validate that a fusion reaction has 
occurred a means to detect neutrons is required.

There are a couple of ways to detect neutrons. The approach I took
makes use of a Helium-3 gas proportional detector. Refer to
https://en.wikipedia.org/wiki/Neutron_detection#Gas_proportional_detectors

Links to my fusor and to a following power supply upgrade are here:
  https://fusor.net/board/viewtopic.php?f=6&t=11581
  https://fusor.net/board/viewtopic.php?f=11&t=13134

My fusor utilized 2 Raspberry Pi computers for data collection and display.
One of the Raspberry Pi was dedicated to the neutron counter function, and
it provided neutron count values to the other Raspberry Pi. The function of the
other was to collect and display data, including: voltage, current, 
pressure, neutron-count, and video.

I am now in the process of dismantling the fusor and preparing to sell the
parts. I hope to be able to help other fusor hobbyists.

To prepare the neutron counter for sale I have updated the software, so that
the neutron counter Raspberry Pi will now display plots of the count rate vs.
time, and count rate histogram. The software is provided in this github
repository. This software re-uses code that was used in the fusor. However, 
the code in main.c, which displays the neutron count rate plots, is all new.

====================================
HARDWARE DIAGRAM
====================================

   ----------      --------                   -----------
   |He-3    |      |Ludlum|      -----        |Raspberry|---- Monitor
   |Neutron |------|2929  |------|ADC|--usb---|Pi       |
   |Detector|   (1)|Scaler|(2)   -----        |         |---- Keyboard
   |Tube    |      --------                   -----------
   ----------

(1) Ludlum front panel, Detector Input Connection:
    "Detector Input Connection: a series “C” coaxial connector used to supply
     the detector with its bias voltage and also to return the signal from the
     detector."
(2) Ludlum rear panel, Amp-Out:
   "Amp Out: a BNC connector that provides access to the final amplifier stage. The
    pulse is positive-going with a maximum amplitude of approximately 22 V"

Components:
- He-3 Neutron Detector Proportional Counter Tube SI-19N СИ-19Н
    Here is a recent link on ebay, I did not purchase from this site.
    https://www.ebay.com/itm/233659189474?hash=item36672c48e2:g:CsoAAOSw1PxewniE
- Ludlum Model 2929 Dual Channel Scaler
    https://ludlums.com/images/product_manuals/M2929.pdf
- ADC, 500k samples/sec
    https://microdaq.com/measurement-computing-usb-204-daq.php
- Raspberry Pi 3 Model B (Revision: a02082) (1GB RAM, 32GB SDcard)

Notes regarding the He-3 SI-19N Proportional Counter Tube
- I set the bias voltage to 1700V.
- I have seen various statements about bias voltage for this detector tube:
  - Onset of counting voltage: 1600V
  - Recommended operating voltage: 2000-2800V (corona mode
  - Recommended operating voltage: 1600V (1500-2800V)
  - Operating Voltage - 2000-2800V
  - Count Start Voltage - 1600-1750V
  - Recommended bias voltage - 1400V (proportional mode)

Notes regarding the Ludlum 2929:
- The Ludlum 2929 is intended to count Alpha and Beta-Gamma. And the 
  hardware pulse height analyzer is factory calibrated to be used with the 
  Model 43-10-1 Alpha/Beta Sample Counter.
- I chose the Ludlim 2929 because it provides the rear panel Amp-Out connection;
  which I use with the ADC and software to scan the signal for pulses.
- The Ludlum 2929 manual contains a Calibration Procedures section; which describes
  how to calibrate the Beta pulse threshold/window, and the Alpha threshold. It may
  be possible to change the Ludlum calibration for use with the SI-19N neutron 
  detector tube. But, the procedure is complicated, and I do not recomend this approach.

Notes regarding the Measurement-Computing-USB-204 ADC
- The 500k samples/sec ADC rate is achieved only if the ADC is configured for 1 channel.
- TechTip: Raspberry Pi Data Acquisition using MCC DAQ and Third-Party Linux Drivers
     https://www.mccdaq.com/TechTips/TechTip-9.aspx

Parts List:
- He-3 Detector Tube
- Ludlum 2929 Scaler
- ADC Measurement-Computing-USB-204 ADC
- Raspberry Pi 3 Model B, keyboard, monitor, power supply
- cables
- HDPE moderator

====================================
NEUTRON COUNTER SOFTWARE
====================================

------------------
---- Overview ----
------------------

The program runs in either Live Mode or Playback Mode. 
- Live Mode:
  - analog pulse data is read from the ADC, the data is scanned for pulses.
  - pulse count rate data is stored in file neutron_yyyy-mm-dd_hh-mm-ss.dat
- Playback Mode: 
  - an exsiting file of pulse count rate data is read when the program starts
  - the ADC is not used in Playback Mode
- Both Modes:
  - pulse count rate is displayed in either a time series plot, or a histogram
  - program log output is written to neutron.log

In both Live and Playback modes, the program has option to display either:
- plot of pulse count rate (units=CPM) vs time, or
- histogram of pulse count rate vs pulse height.

  When displaying the plot of pulse count rate vs time, the pulse count rate is displayed 
  in green when in Live Mode and it is being displayed for the current time.
  Otherwise the pulse count rate is displayed in red.

  When displaying the histogram, the histogram buckets that are included in the calculation
  of the pulse count rate are displayed in cyan.

-----------------------
---- Usage Details ----
-----------------------

To run the program, login neutron, cd proj_neutron.

Usage: neutron [-p <filename.dat] [-v <select>] [-h]
         -p <filename.dat> : playback mode
         -v <select>       : enable verbose logging, select=0,1,2,all
         -h                : help

Program settings:
- avg_intvl:  the duration (in seconds) of each plot data point
- pht:        pulse height threshold, pulses that have heights >= pht are
              included in the calculation of the pulse count rate
- y_max:      y axis maximum value for the plot and histogram

Program Controls:
- Display Selection
    F1  F2     :  select either Plot or Histogram display
- Adjust Settings:
    PgUp  PgDn :  adjust y_max
    -     =    :  adjust avg_intvl
    1     2    :  adjst pht
- Adjust Time:
    Left  Right : by avg_intvl
    ,     .     : by 1 second
    <     >     : by 1 minute
    k     l     : by 1 hour
    Home        : move to begining of data
    End         : move to end of data, and enable live mode 
                  automatic time adjustment
- Misc
    s     r     : save and recall parameters
    R           : reset parameters to default values
    q           : quit program

--------------------------
---- Verbose Logging  ----
--------------------------

-v0:  xxx

-v1:  prints pulses to the log file. 
      Pulses a printed at most once per second, to avoid using too much log file space or 
      too much CPU time.  An example of a pulse printed to the logfile:
          PULSE:  height = 309   baseline = 2386
            338: *********************************+
            647: *********************************+*******************************
            583: *********************************+*************************
            404: *********************************+*******
            350: *********************************+**
            339: *********************************+
            336: *********************************+
            336: *********************************+
            (1)                                  (2)
          (1) is the value from the ADC minus 2048
                -2048 => -10000 millivolt
                    0 =>      0  
                 2047 => +10000
          (2) baseline level, which is the value read from the ADC when there 
              is not a pulse present

-v2:  xxx

------------------------
---- Program Design ----
------------------------

To build, run make. My Raspberry Pi has the build environment installed. To build
on another computer, you must install the mccdaq software, refer to
http://www.mccdaq.com/TechTips/TechTip-9.aspx for instructions.

When in Playback Mode, only the code in main.c is used. When in Live Mode, the
code in util_mccdaq.c and mccdaq_cb.c is used as well.

Source code files ...

main.c:
- Maintains an array of pulse count data: 'static pulse_count_t  data[MAX_DATA];'
  - When in playback mode, this array is filled by the initialize() routine, using data from a file.
  - When in live mode, new entries are added to this array, once per second, by the 
    publish() routine. The publish routine is called in the following flow:
        mccdaq_consumer_thread()  util_mccdaq.c: This thread detects that data from the
                |                 ADC is available, and calls mccdaq_callback (g_cb) with
                |                 this data.
                v
        mccdaq_callback()         mccdaq_cb.c: This routine scans the ADC data for pulses,
                |                 and keeps track of the number of pulses and their heights
                |                 in the pulse_count.bucket[] array. Each element of the
                |                 bucket array is for a different range of pulse height. Once per
                |                 second the pulse_count is passed to publish(), and the pulse_count
                |                 is then zeroed in preparation for analyzing the next second of
                |                 ADC data.
                v
        publish()                 main.c: This routine appends the pulse_count input data, to
                                  the data array that is defined in main.c
- Uses the curses library to draw a plot of CPM vs time, or CPM vs histogram bucket.
- When in Live Mode, the live_mode_write_data_thread monitors for newly published pulse_count_t
  being added to the data[] array. And when new data is added, this thread will write the data to
  the neutron_yyyy-mm-dd_hh-mm-ss.dat file.

util_mccdaq.c:
- mccdaq_producer_thread: reads data from the ADC, using USB; and stores the values in g_data
- mccdaq_consumer_thread: detects when new ADC values are available in g_data, and calls
  g_cb (mccdaq_callback), passing the new ADC values to mccdaq_callback

mccdaq_cb.c:
- the mccdaq_callback() routine scans the ADC data for pulses, and calls the publish() routine
  (in main.c), once per second, with the pulse count values.
  
=======================================================================
=======================================================================
=======================================================================

xxxx add pictures and screenshots
----------------------------------
