====================================
INTRODUCTION
====================================

I am a fusor hobbyist, and I have used the information provided
on fusor.net to make a working fusor in my basement. To validate that
a fusion reaction has occurred a means to detect neutrons is needed.

There are a couple of ways to detect neutrons. The approach I took
makes use of a Helium-3 gas proportional detector. Refer to
https://en.wikipedia.org/wiki/Neutron_detection#Gas_proportional_detectors

Links to my fusor and to a following power supply upgrade are here:
https://fusor.net/board/viewtopic.php?f=6&t=11581
https://fusor.net/board/viewtopic.php?f=11&t=13134

My fusor utilized 2 Raspberry Pi computers for data collection and display.
One of the Raspberry Pi was dedicated to the neutron counter function, and
it sent neutron count values to the other Raspberry Pi. The function of the
other was to collect and display data, which included: voltage, current, 
pressure, neutron-count, and video.

I am now in the process of dismantling the fusor and preparing to sell the
parts, hopefully to other fusor hobbyists.

To prepare the neutron counter for sale I have updated the software, so that
the neutron counter Raspberry Pi will now display plots of the count rate vs.
time, and count rate histogram. The software is contained in this github
repository. This software re-uses c language code that was used in the fusor.
However, the code in main.c, which displays the neutron count rate plots, is new.

XXX how I tested

====================================
HARDWARE DIAGRAM
====================================

   ----------      --------                   -----------
   |He-3    |      |Ludlum|      -----        |Raspberry|---- Monitor
   |Neutron |------|2929  |------|ADC|--usb---|Pi       |
   |Detector|   (1)|Scaler|(2)   -----        |         |---- Keyboard
   |Tube    |      --------                   -----------
   ----------

(1) Ludlum front panel Detector Input Connection:
    "Detector Input Connection: a series “C” coaxial connector used to supply
     the detector with its bias voltage and also to return the signal from the
     detector."
(2) Ludlum read panel Amp-Out:
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
- I have seen various statements about bias voltage for this tube:
  - Onset of counting voltage: 1600V
  - Recommended operating voltage: 2000-2800V (corona mode
  - Recommended operating voltage: 1600V (1500-2800V)
  - Operating Voltage - 2000-2800V
  - Count Start Voltage - 1600-1750V
  - Recommended bias voltage - 1400V (proportional mode)

Notes regarding the Ludlum 2929:
- The Ludlum 2929 is intended to count Alpha and Beta-Gamma. And the pulse height
  analyzer is factory calibrated to be used with the 
  Model 43-10-1 Alpha/Beta Sample Counter.
- I chose the Ludlim 2929 because of it provides the rear panel Amp-Out connection;
  which I use with the ADC and software to analyze pulse height.
- The Ludlum 2929 manual contains a Calibration Procedures section; which describes
  how to calibrate the Beta pulse threshold/window, and the Alpha threshold. It may
  be possible to change the Ludlum calibration for use with the SI-19N neutron 
  detector tube. But, the procedure is complex, and I do not recomend this approach.

Notes regarding the Measurement-Computing-USB-204 ADC
- The 500k samples/sec ADC rate is achieved only if the ADC is configured for 1 channel.
- TechTip: Raspberry Pi Data Acquisition using MCC DAQ and Third-Party Linux Drivers
     https://www.mccdaq.com/TechTips/TechTip-9.aspx

Parts List:
- xxx

====================================
NEUTRON COUNTER SOFTWARE USAGE
====================================

---- Overview ----

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
- plot of pulse count rate (units=CPM) vs Time, or
- histogram of pulse count rate vs pulse height.

  When displaying the plot of pulse count rate vs time, the pulse count rate is displayed 
  in green when in Live Mode and the it is being displayed for the current time.
  Otherwise the pulse count rate is displayed in red.

  When displaying the histogram, the histogram buckets that are included in the calculation
  of the pulse count rate are displayed in cyan.

---- Details ----

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

---- Verbose Logging  ----

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

====================================
NEUTRON COUNTER SOFTWARE DESGIN
====================================

Overview 

Live mode vs Playback mode diffs

Code Flow


xxxx add pictures and screenshots
----------------------------------
