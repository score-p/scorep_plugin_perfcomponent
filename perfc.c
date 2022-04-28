/*
 * Copyright (c) 2016, Technische Universität Dresden, Germany
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 *    and the following disclaimer in the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef VT
#include <vampirtrace/vt_plugin_cntr.h>
#else
#ifdef SCOREP
#include <scorep/SCOREP_MetricPlugins.h>
#else
#error "You need Score-P or VampirTrace to compile this plugin"
#endif /* SCOREP*/
#endif /* VT */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/syscall.h>

#include <linux/perf_event.h>

/* defines a specific event. Unfortunately perf introduced scale. Fuck them! */
struct event{
  int fd;
  double scale;
};

/* current_registered events */
static struct event events[4096] ;
static int num_events = 0;

pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;


/* init and fini do not do anything */
/* This is intended! */
int32_t init(){
  return 0;
}
void fini(){
  /* close the file handles from perf */
  int i;
  for (i=0;i<num_events;i++)
    close(events[i].fd);
}

/* store some information within a 64 bit bit field on bits start:<(int)ld(value)> */
static void inline write_at_position(uint64_t * config, int start, unsigned long long value){
  (* config) |= (value << start);
}


/* This function writes the attr definitions for a given event name
 * It provides the unit and a scale for this attr
 * If the event has not been found, returns ! 0
 * */
int build_perf_attr(struct perf_event_attr * attr, const char * name, const char * unit, double * scale){
  int                i, j, k, nr_definitions;
  FILE * fd;
  char tmp[256],reading[256];
  char event_source_name[256];
  char event_name[256];
  char definitions[16][32];
  char definition_readings[16][32];
  uint64_t * active_config;
  long config_mask_start, config_mask_end;
  char config_type[16];
  char * strtok_r_saveptr = NULL;
  char * position, * position2;
  memset( attr, 0, sizeof( struct perf_event_attr ) );
  attr->config = 0;
  attr->config1 = 0;
  attr->type    = 0;
  /* first part of the name is e.g. power, then comes the event */
  position = strchr(name,'/');
  if (!position){
    /* malformed event */
    fprintf(stderr, "The event %s is malformed, it should be <event_source>/<event>\n",name);
    return 1;
  }
  /* end the event source string for the first strncpy */
  *position=0;
  strncpy(event_source_name,name,255);
  strncpy(event_name,position+1,255);

  /* re-establish the original string */
  *position='/';
  /* We try to open /sys/bus/event_source/power/type, to get the type */
  snprintf(tmp,256,"/sys/bus/event_source/devices/%s/type",event_source_name);
  fd = fopen(tmp,"r");
  if ( fd <= 0 ){
    /* event source does not exist */
    fprintf(stderr, "Unable to open %s\n",tmp);
    return errno;
  }
  i=fscanf(fd,"%d",&(attr->type));
  fclose(fd);
  if ( i < 1 ){
    /* could not read type */
    fprintf(stderr, "Unable to read %s\n",tmp);
    return i;
  }
  /* open raw descriptors */
  /* check type of event. it could be predefined or sophisticated */
  /* predefined: power/energy-cores */
  /* sophisticated uncore_imc_0/event=0xff,umask=0x00/*/
  if (strstr(event_name,"=")==NULL){
    /* predefined */

    /* check for unit and scale */
    snprintf(tmp,255,"/sys/bus/event_source/devices/%s/events/%s.unit",event_source_name,event_name);
    /* read the unit */
    fd = fopen(tmp,"r");
    if ( fd <= 0 ){
      /* event no unit */
      unit = NULL;
    } else {
      i=fscanf(fd,"%s",unit);
      fclose(fd);
      if ( i < 1 ){
        /* could not read event */
        fprintf(stderr, "Unable to read %s\n",tmp);
        return i;
      }
    }
    snprintf(tmp,255,"/sys/bus/event_source/devices/%s/events/%s.scale",event_source_name,event_name);
    /* read the unit */
    fd = fopen(tmp,"r");
    if ( fd <= 0 ){
      /* no scale avail */
      *scale=1.0;
      fprintf(stderr, "no scale!: %e\n",*scale);
    } else {
      i=fscanf(fd,"%s",reading);
      if ( i < 1 ){
        /* could not read event */
        fprintf(stderr, "Unable to read %s\n",tmp);
        return i;
      }
      *scale  = strtod(reading,NULL);
      fclose(fd);
    }

    /* "rename" the event to sophisticated by reading the event definition */
    snprintf(tmp,255,"/sys/bus/event_source/devices/%s/events/%s",event_source_name,event_name);
    /* read the definition */
    fd = fopen(tmp,"r");
    if ( fd <= 0 ){
      /* event definition does not exist */
      fprintf(stderr, "Unable to open %s\n",tmp);
      return errno;
    }
    /* a  typical line is event=0xcd,umask=0x2event=0x00,umask=0x03*/
    i=fscanf(fd,"%s",event_name);
    fclose(fd);
    if ( i < 1 ){
      /* could not read event */
      fprintf(stderr, "Unable to read %s\n",tmp);
      return i;
    }
  }
  /* now we have a sophisticated event in event_name */
  /* e.g. "event=0xcd,umask=0x2" */
  /* we now tokenize them */
  position = strtok_r(event_name,",",&strtok_r_saveptr);
  nr_definitions = 0;
  while ( (position != NULL) && (nr_definitions < 16) )
  {
    /* get the definitions and definition_readings */
    /* example: event=0xcd,*/
    i = sscanf(position,"%[^=]=%s",definitions[nr_definitions],definition_readings[nr_definitions]);
    if (i < 2){
      fprintf(stderr, "Error while parsing first event in this list: %s\n",position);
      return -1;
    }
    /* next */
    position = strtok_r(NULL,",",&strtok_r_saveptr);
    nr_definitions++;
  }
  if (nr_definitions == 16) {
    fprintf(stderr, "The event %s is to complex for this plugin, please inform the developer (%d) \n",event_name,nr_definitions);
    return 16;
  }
  /* now we have to check the /sys/bus/event_source/<event_source_name>/format/<definition[i]> files */
  for (i=0;i<nr_definitions;i++){
    /* get the value to set */
    unsigned long long value = strtoull(definition_readings[i], &position, 16);
    if ( position == definition_readings[i] ){
      fprintf(stderr, "Malformed definition of event this should be a hex-value: %s\n",
          definition_readings[i]);
      return -1;
    }
    /* read the definition */
    snprintf(tmp,255,"/sys/bus/event_source/devices/%s/format/%s",event_source_name,definitions[i]);
    fd = fopen(tmp,"r");
    if ( fd <= 0 ){
      /* event definition does not exist */
      fprintf(stderr, "Unable to open %s\n",tmp);
      return errno;
    }
    /* this should be config-type:mask */
    /* the typical line says config:0-7 */
    int check = fscanf(fd,"%s", tmp);
    if ( check < 0 ){
      fprintf(stderr, "Error while reading file /sys/bus/event_source/devices/%s/format/%s",event_source_name,definitions[i]);
      return -1;
    }
    fclose(fd);
    /* go through tmp */
    /* skip to after "config" */
    position=&tmp[6];
    j=atoi(position);
    switch (j){
      case 0 : active_config = (uint64_t *)&(attr->config); break;
      case 1 : active_config = (uint64_t *)&(attr->config1); break;
      case 2 : active_config = (uint64_t *)&(attr->config2); break;
      default: fprintf(stderr, "Unhandled config flag %d\n",j); return j;
    }
    /* get the mask */
    position=strstr(tmp,":");
    if ( position == NULL ){
      fprintf(stderr, "Malformed definition %s\n",tmp);
      return -1;
    }
    /* go to after ":" */
    position++;
    /* get start */
    config_mask_start = strtol(position,&position2,10);
    if ( position2 == position ){
      /* no value ?! */
      fprintf(stderr, "Malformed definition (wrong start value) %s\n",tmp);
      return -1;
    }
    if (*position2==0){
      /* no end, just a binary information */
      if ( value > 1 ){
        /* no value ?! */
        fprintf(stderr, "Provided integer setting (%llu) "
            "for binary definition (%s) on event %s\n",
            value,
            definitions[i],
            event_name);
        return -1;
      }
      else {
        write_at_position(active_config,config_mask_start,value);
      }
    } else {
      /* has start and end bit */

      /* get the mask */
      position=strstr(position,"-");
      if ( position == NULL ){
        fprintf(stderr, "Malformed definition %s\n",tmp);
        return -1;
      }
      /* go to after "-" */
      position++;
      /* get start */
      config_mask_end = strtol(position,&position2,10);
      if ( position2 == position ){
        /* no value ?! */
        fprintf(stderr, "Malformed definition (wrong end value) %s\n",tmp);
        return -1;
      }
      if ( value >= (1ULL<<(config_mask_end-config_mask_start+1)) ){
        /* no value ?! */
        fprintf(stderr, "Provided setting (%llx) that is too large"
            "for binary definition (%s) of length %lu on event %s\n",
            value,
            definitions[i],
            config_mask_end-config_mask_start,
            event_name);
        return -1;
      }
      else {
        write_at_position(active_config,config_mask_start,value);
      }
    }
  }
  return 0;

}
/* registers perf event */
int32_t add_counter(char * event_name){
  int fd,ret ;
  struct perf_event_attr attr;
  char unit[256];
  double scale=1.0;
  if (num_events == 4096) {
    fprintf(stderr, "PerfC Plugin does only support 4096 metrics per process\n");
    return -1;
  }

  pthread_mutex_lock(&event_lock);
  ret = build_perf_attr(&attr, event_name ,unit , &scale);
  /* wrong metric */
  if (ret){
    fprintf(stderr, "PERF metric not recognized: %s", event_name );
    pthread_mutex_unlock(&event_lock);
    return -1;
  }
  pthread_mutex_unlock(&event_lock);
  
  fd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, 0);
  if (fd<=0){
    fprintf(stderr, "Unable to open counter \"%s\". Aborting.\n",event_name);
    return -1;
  }
  
  pthread_mutex_lock(&event_lock);
  if (num_events == 4096){
    fprintf(stderr, "PerfC Plugin does only support 4096 metrics per process\n");
    close(fd);
    pthread_mutex_unlock(&event_lock);
    return -1;
  }
  ret=num_events;
  num_events++;
  pthread_mutex_unlock(&event_lock);
  
  events[ret].fd=fd;
  events[ret].scale=scale;
  return ret;
}

/* reads value */
uint64_t get_value(int index){
  union{
    uint64_t u;
    double d;
  } count;
  size_t res=read(events[index].fd, &count.u, sizeof(uint64_t));
  count.d=(double)events[index].scale*count.u;
  if (res!=sizeof(uint64_t))
    return !0;
  return count.u;
}

#ifdef VT

vt_plugin_cntr_metric_info * get_event_info(char * event_name){
    vt_plugin_cntr_metric_info * return_values;
    int ret;
  struct perf_event_attr attr;
  char unit[256]="";
  double scale;

  ret = build_perf_attr(&attr, event_name ,unit , &scale);
  /* wrong metric */
  if (ret){
    fprintf(stderr, "PERF metric not recognized: %s", event_name );
    return NULL;
  }
  return_values=
            malloc(2 * sizeof(vt_plugin_cntr_metric_info) );
  if (return_values==NULL){
    fprintf(stderr, "VampirTrace PerfC Plugin: failed to allocate memory for passing information to VT.\n");
    return NULL;
  }
    return_values[0].name=strdup(event_name);
    if (unit[0]=='\0')
      return_values[0].unit=NULL;
    else
    return_values[0].unit=strdup(unit);
    return_values[0].cntr_property=VT_PLUGIN_CNTR_LAST|VT_PLUGIN_CNTR_START|
     VT_PLUGIN_CNTR_DOUBLE;
    return_values[1].name=NULL;
    return return_values;
}


/**
 * This function get called to give some informations about the plugin to VT
 */
vt_plugin_cntr_info get_info(){
  vt_plugin_cntr_info info;
  char * env;
  memset(&info,0,sizeof(vt_plugin_cntr_info));
  info.init              = init;
  info.add_counter       = add_counter;
  info.vt_plugin_cntr_version = VT_PLUGIN_CNTR_VERSION;
  info.run_per           = VT_PLUGIN_CNTR_PER_THREAD;
  env = getenv("VT_PLUGIN_PERFC_HOST");
  if (env){
    if (! strcmp(env, "TRUE") ||
        ! strcmp(env, "True") ||
        ! strcmp(env, "true") ||
        ! strcmp(env, "1"))
      info.run_per        = VT_PLUGIN_CNTR_PER_HOST;
  }
  info.synch             = VT_PLUGIN_CNTR_SYNCH;
  env = getenv("VT_PLUGIN_PERFC_ASYNC");
  if (env){
    if (! strcmp(env, "TRUE") ||
        ! strcmp(env, "True") ||
        ! strcmp(env, "true") ||
        ! strcmp(env, "1"))
      fprintf(stderr,"Asynchronous support for PerfC plugin not supported yet\n");
  }
  info.get_event_info    = get_event_info;
  info.get_current_value = get_value;
  info.finalize          = fini;
  return info;
}
#elif SCOREP

SCOREP_Metric_Plugin_MetricProperties * get_event_info(char * event_name)
{
  SCOREP_Metric_Plugin_MetricProperties * return_values;
  int ret;
  struct perf_event_attr attr;
  char unit[256];
  double scale;

  ret = build_perf_attr(&attr, event_name ,unit , &scale);
  /* wrong metric */
  if (ret){
    fprintf(stderr, "PERF metric not recognized: %s", event_name );
    return NULL;
  }
  return_values=
      malloc(2 * sizeof(SCOREP_Metric_Plugin_MetricProperties) );
  if (return_values==NULL){
    fprintf(stderr, "Score-P PerfC Plugin: failed to allocate memory for passing information to Score-P.\n");
    return NULL;
  }
  return_values[0].name        = strdup(event_name);
  if (unit[0]=='\0')
    return_values[0].unit=NULL;
  else
    return_values[0].unit=strdup(unit);
  return_values[0].description = NULL;
  return_values[0].mode        = SCOREP_METRIC_MODE_ACCUMULATED_START;
  return_values[0].value_type  = SCOREP_METRIC_VALUE_DOUBLE;
  return_values[0].base        = SCOREP_METRIC_BASE_DECIMAL;
  return_values[0].exponent    = 0;
  return_values[1].name=NULL;
  return return_values;
}

bool get_optional_value( int32_t   id,
                               uint64_t* value ){
  *value=get_value(id);
  return true;
}

/**
 * This function get called to give some informations about the plugin to scorep
 */
SCOREP_METRIC_PLUGIN_ENTRY( perfcomponent_plugin )
{
    char * env;
    /* Initialize info data (with zero) */
    SCOREP_Metric_Plugin_Info info;
    memset( &info, 0, sizeof( SCOREP_Metric_Plugin_Info ) );

    /* Set up the structure */
    info.plugin_version               = SCOREP_METRIC_PLUGIN_VERSION;
    info.run_per                      = SCOREP_METRIC_PER_THREAD;
    info.sync                         = SCOREP_METRIC_SYNC;
    info.initialize                   = init;
    info.finalize                     = fini;
    info.get_event_info               = get_event_info;
    info.add_counter                  = add_counter;
    info.get_current_value            = get_value;
    info.get_optional_value           = get_optional_value;

    /* non-default execution settings */
    env = getenv("SCOREP_METRIC_PERFCOMPONENT_HOST");
    if (env){
      if (! strcmp(env, "TRUE") ||
          ! strcmp(env, "True") ||
          ! strcmp(env, "true") ||
          ! strcmp(env, "1"))
        info.run_per = SCOREP_METRIC_PER_HOST;
    }
    
    /* non-default execution settings */
    env = getenv("SCOREP_METRIC_PERFCOMPONENT_DELTA_TIME");
    if (env){
      info.delta_t=atoll(env);
    }
    
    env = getenv("SCOREP_METRIC_PERFCOMPONENT_ASYNC");
    if (env){
      if (! strcmp(env, "TRUE") ||
          ! strcmp(env, "True") ||
          ! strcmp(env, "true") ||
          ! strcmp(env, "1"))
        fprintf(stderr,"Asynchronous support for Perf Component plugin not supported yet.\n");
    }

    return info;
}
#endif /* SCOREP */
