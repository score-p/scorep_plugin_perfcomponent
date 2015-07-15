#VampirTrace/Score-P perf Plugin Counter Component

##Compilation and Installation

###Prerequisites

To compile this plugin, you need:

* C compiler

* CMake

* VampirTrace or Score-P

* A recent Linux kernel (`2.6.32+`) with activated tracing and the kernel headers

###Building

1. Create a build directory

        mkdir build
        cd build

2. Invoke CMake

    Specify the VampirTrace and/or Score-P directory if it is not in the default path with
    `-DVT_INC=<PATH>` respectivly `-DSCOREP_DIR=<PATH>`. The plugin will use alternatively the
    environment variables `VT_DIR` and `SCOREP_DIR`, e.g.

        cmake .. -DVT_DIR=/opt/vampirtrace -DSCOREP_DIR=/opt/scorep

3. Invoke make

        make

4. Copy it to a location listed in `LD_LIBRARY_PATH` or add current path to `LD_LIBRARY_PATH` with

        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:`pwd`

##Usage

Before using this, be sure that you are allowed to measure the counters. It might be the case, that
you have to change your paranoid level in `/proc/sys/kernel/perf_event_paranoid`. It should be 0 or
-1.

Paranoid levels:

 *  -1 - not paranoid at all

 *   0 - disallow raw tracepoint access for unpriv

 *   1 - disallow cpu events for unpriv

 *   2 - disallow kernel profiling for unpriv

To add a kernel event counter to your trace, you have to specify the environment variable
`VT_PLUGIN_CNTR_METRIC` respectivly `SCOREP_METRIC_PLUGINS`.

`VT_PLUGIN_CNTR_METRIC`/`SCOREP_METRIC_PLUGINS` specifies the software events that shall be recorded
when tracing an application. You can add metrics that are available on your system. These can be
predefined or explicit. Each event consists of an event source and an event definition.

Please check your `/sys/bus/event_source/devices/` folder for event sources, e.g.
`/sys/bus/event_source/devices/uncore_imc_0/` is the integrated memory controller of NUMA node 0 on
Intel Sandy Bridge EP or `/sys/bus/event_source/devices/power/` on some systems with RAPL
available.

In these folders there might be predefined events, e.g., some systems provide:

    /sys/bus/event_source/devices/power/events/energy-cores

These events can be used additional to explicit ones. Examples are:

* Explicit

    "uncore_imc_0/event=0xff,umask=0x00" (memory controller cycles on sandy Bridge)

* Predefined

    "power/energy-cores" (memory controller cycles on sandy Bridge)

Usage for both in VampirTrace

    export VT_PLUGIN_CNTR_METRICS="PerfCVT_uncore_imc_0/event=0xff,umask=0x00"
    export VT_PLUGIN_CNTR_METRICS="PerfCVT_power/energy-cores"

Usage for the latter in Score-P

    export SCOREP_METRIC_PLUGINS="PerfC"
    export SCOREP_METRICS_PERFC="uncore_imc_0/event=0xff,umask=0x00"
    export SCOREP_METRICS_PERFC="power/energy-cores"

###If anything fails

1. Check whether the plugin library can be loaded from the `LD_LIBRARY_PATH`.

2. Check your paranoid level in `/proc/sys/kernel/perf_event_paranoid`. It should be 0 or -1.

3. Write a mail to the author.

##Author

* Robert Schoene (robert.schoene at tu-dresden dot de)
