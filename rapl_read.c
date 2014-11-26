/*   
 *   File: rapl_read.c
 *   Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *   Description: 
 *   rapl_read.c is part of ASCYLIB
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *	      	      Distributed Programming Lab (LPD), EPFL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "rapl_read.h"

int rapl_cpu_model;
int rapl_msr_fd[NUMBER_OF_SOCKETS];

#define RAPL_INIT_OFFS 17
int rapl_initialized[NUMBER_OF_SOCKETS] = {};
int rapl_resp_core[NUMBER_OF_SOCKETS] = {};
int rapl_dram_counter = 0;
uint32_t rapl_num_active_sockets = 0;
double rapl_power_units, rapl_energy_units, rapl_time_units;
__thread int rapl_core;
__thread int rapl_socket;
double rapl_package_before[NUMBER_OF_SOCKETS], rapl_package_after[NUMBER_OF_SOCKETS], rapl_pp0_before[NUMBER_OF_SOCKETS], 
  rapl_pp0_after[NUMBER_OF_SOCKETS], rapl_pp1_before[NUMBER_OF_SOCKETS], rapl_pp1_after[NUMBER_OF_SOCKETS], 
  rapl_dram_before[NUMBER_OF_SOCKETS], rapl_dram_after[NUMBER_OF_SOCKETS];
double rapl_thermal_spec_power, rapl_minimum_power, rapl_maximum_power, rapl_time_window;
double rapl_pkg_power_limit_1, rapl_pkg_time_window_1, rapl_pkg_power_limit_2, rapl_pkg_time_window_2;
double rapl_acc_pkg_throttled_time, rapl_acc_rapl_pp0_throttled_time;
long long int rapl_msr_pkg_settings;
int rapl_pp0_policy, rapl_pp1_policy;

uint64_t rapl_start_ts[NUMBER_OF_SOCKETS], rapl_stop_ts[NUMBER_OF_SOCKETS];

#define FOR_ALL_SOCKETS(s)			\
  for (s = 0; s < NUMBER_OF_SOCKETS; s++)


int
open_msr(int core) 
{
  char msr_filename[BUFSIZ];
  int fd;

  sprintf(msr_filename, "/dev/cpu/%d/msr", core);
  fd = open(msr_filename, O_RDONLY);
  if (fd < 0) 
    {
      if (errno == ENXIO) 
	{
	  fprintf(stderr, "rdmsr: No CPU %d\n", core);
	  exit(2);
	} 
      else if (errno == EIO) 
	{
	  fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", core);
	  exit(3);
	} 
      else 
	{
	  perror("rdmsr:open");
	  fprintf(stderr,"Trying to open %s\n",msr_filename);
	  exit(127);
	}
    }
  return fd;
}

long long int
read_msr(int fd, int which) 
{
  uint64_t data;

  if (pread(fd, &data, sizeof(data), which) != sizeof(data)) 
    {
      perror("rdmsr:pread");
      exit(127);
    }

  return (long long) data;
}

int
detect_cpu(void) 
{
  FILE *fff;

  int family, model=-1;
  char buffer[BUFSIZ], *result;
  char vendor[BUFSIZ];

  fff = fopen("/proc/cpuinfo","r");
  if (fff == NULL) 
    {
      return -1;
    }

  while(1) 
    {
      result=fgets(buffer,BUFSIZ,fff);
      if (result == NULL) 
	{ 
	  break;
	}

      if (!strncmp(result,"vendor_id",8)) 
	{
	  sscanf(result,"%*s%*s%s",vendor);

	  if (strncmp(vendor,"GenuineIntel",12)) 
	    {
	      printf("%s not an Intel chip\n",vendor);
	      return -1;
	    }
	}

      if (!strncmp(result,"cpu family",10)) 
	{
	  sscanf(result,"%*s%*s%*s%d",&family);
	  if (family != 6) 
	    {
	      printf("Wrong CPU family %d\n",family);
	      return -1;
	    }
	}

      if (!strncmp(result,"model",5)) 
	{
	  sscanf(result,"%*s%*s%d",&model);
	}
    }

  fclose(fff);

  switch(model) 
    {
    case CPU_SANDYBRIDGE:
      // printf("[RAPL] Found Sandybridge CPU\n");
      break;
    case CPU_SANDYBRIDGE_EP:
      // printf("[RAPL] Found Sandybridge-EP CPU\n");
      break;
    case CPU_IVYBRIDGE:
      // printf("[RAPL] Found Ivybridge CPU\n");
      break;
    case CPU_IVYBRIDGE_EP:
      // printf("[RAPL] Found Ivybridge-EP CPU\n");
      break;
    case CPU_HASWELL:
      // printf("[RAPL] Found Haswell CPU\n");
      break;
    default:	
      printf("[RAPL] Unsupported model %d\n", model);
      model=-1;
      break;
    }

  return model;
}

static inline int
rapl_get_core_with_offs()
{
  return (rapl_core + RAPL_INIT_OFFS);
}

static inline int
rapl_allowed()
{
  if (!rapl_initialized[rapl_socket] || rapl_resp_core[rapl_socket] != rapl_get_core_with_offs())
    {
      return 0;
    }
  return 1;
}

static inline int
rapl_allowed_once()
{
  if (!rapl_initialized[rapl_socket] || rapl_resp_core[rapl_socket] != rapl_get_core_with_offs())
    {
      return 0;
    }

  int min_socket = 0;
  while (min_socket < NUMBER_OF_SOCKETS && !rapl_initialized[min_socket])
    {
      min_socket++;
    }
  return (rapl_socket == min_socket);
}




int
rapl_read_init(int core)
{
  rapl_core = core; 
  rapl_socket = get_cluster(core);

  if (rapl_core >= (NUMBER_OF_SOCKETS * CORES_PER_SOCKET))
    {
      return 2;
    }
  
  /* try to be the "guy" for this socket */
  if (__sync_bool_compare_and_swap(rapl_resp_core + rapl_socket, 0, rapl_get_core_with_offs()) == 0)
    {
      return 2;
    }


  __sync_fetch_and_add(&rapl_num_active_sockets, 1);

  rapl_cpu_model = detect_cpu();
  if (rapl_cpu_model < 0)
    {
      printf("[RAPL] Unsupported processor\n");
      return -1;
    }

  if (!((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)))
    {
      rapl_dram_counter = 1;
    }


  rapl_msr_fd[rapl_socket] = open_msr(core);
  if (rapl_msr_fd[rapl_socket] < 0)
    {
      printf("[RAPL] Cannot read MSR\n");
      return -1;
    }

  rapl_initialized[rapl_socket] = 1;


  if (!rapl_allowed_once())
    {
      return 1;
    }
  /* Calculate the units used */
  long long int result = read_msr(rapl_msr_fd[rapl_socket], MSR_RAPL_POWER_UNIT);
  rapl_power_units = pow(0.5, (double)(result&0xf));
  rapl_energy_units = pow(0.5, (double)((result>>8)&0x1f));
  rapl_time_units = pow(0.5, (double)((result>>16)&0xf));


  /* Show package power info */
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_POWER_INFO);
  rapl_thermal_spec_power = rapl_power_units * (double)(result&0x7fff);
  rapl_minimum_power = rapl_power_units * (double)((result>>16)&0x7fff);
  rapl_maximum_power = rapl_power_units * (double)((result>>32)&0x7fff);
  rapl_time_window = rapl_time_units * (double)((result>>48)&0x7fff);


  /* Show package power limit */
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_RAPL_POWER_LIMIT);
  rapl_msr_pkg_settings = result;
  rapl_pkg_power_limit_1 = rapl_power_units * (double)((result>>0)&0x7FFF);
  rapl_pkg_time_window_1 = rapl_time_units * (double)((result>>17)&0x007F);
  rapl_pkg_power_limit_2 = rapl_power_units * (double)((result>>32)&0x7FFF);
  rapl_pkg_time_window_2 = rapl_time_units * (double)((result>>49)&0x007F);

  return 1;
}

int
rapl_read_init_all()
{
  __sync_fetch_and_add(&rapl_num_active_sockets, NUMBER_OF_SOCKETS);

  rapl_cpu_model = detect_cpu();
  if (rapl_cpu_model < 0)
    {
      printf("[RAPL] Unsupported processor\n");
      return -1;
    }

  if (!((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)))
    {
      rapl_dram_counter = 1;
    }


  int s;
  FOR_ALL_SOCKETS(s)
  {
    int core;
    for (core = 0; core < NUMBER_OF_SOCKETS * CORES_PER_SOCKET; core++)
      {
	if (s == get_cluster(core))
	  {
	    rapl_msr_fd[s] = open_msr(core);
	    break;
	  }
      }

    if (rapl_msr_fd[s] < 0)
      {
	printf("[RAPL] Cannot read MSR\n");
	return -1;
      }

    rapl_initialized[s] = 1;
  }
 
  s = 0;
  /* Calculate the units used */
  long long int result = read_msr(rapl_msr_fd[s], MSR_RAPL_POWER_UNIT);
  rapl_power_units = pow(0.5, (double)(result&0xf));
  rapl_energy_units = pow(0.5, (double)((result>>8)&0x1f));
  rapl_time_units = pow(0.5, (double)((result>>16)&0xf));


  /* Show package power info */
  result = read_msr(rapl_msr_fd[s], MSR_PKG_POWER_INFO);
  rapl_thermal_spec_power = rapl_power_units * (double)(result&0x7fff);
  rapl_minimum_power = rapl_power_units * (double)((result>>16)&0x7fff);
  rapl_maximum_power = rapl_power_units * (double)((result>>32)&0x7fff);
  rapl_time_window = rapl_time_units * (double)((result>>48)&0x7fff);


  /* Show package power limit */
  result = read_msr(rapl_msr_fd[s], MSR_PKG_RAPL_POWER_LIMIT);
  rapl_msr_pkg_settings = result;
  rapl_pkg_power_limit_1 = rapl_power_units * (double)((result>>0)&0x7FFF);
  rapl_pkg_time_window_1 = rapl_time_units * (double)((result>>17)&0x007F);
  rapl_pkg_power_limit_2 = rapl_power_units * (double)((result>>32)&0x7FFF);
  rapl_pkg_time_window_2 = rapl_time_units * (double)((result>>49)&0x007F);
  
  return 1;
}

void
rapl_read_start()
{
  if (!rapl_allowed())
    {
      return;
    }
  long long int result; 

  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);
  rapl_package_before[rapl_socket] = (double)result * rapl_energy_units;

  if ((rapl_cpu_model == CPU_SANDYBRIDGE_EP) || (rapl_cpu_model == CPU_IVYBRIDGE_EP))
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_PERF_STATUS);
      rapl_acc_pkg_throttled_time = (double)result * rapl_time_units;
    }

  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_before[rapl_socket] = (double)result * rapl_energy_units;

  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_POLICY);
  rapl_pp0_policy = (int)result&0x001f;

  /* only available on *Bridge-EP */
  if ((rapl_cpu_model == CPU_SANDYBRIDGE_EP) || (rapl_cpu_model == CPU_IVYBRIDGE_EP))
    {
      result = read_msr(rapl_msr_fd[rapl_socket],MSR_PP0_PERF_STATUS);
      rapl_acc_rapl_pp0_throttled_time = (double)result * rapl_time_units;
    }

  /* not available on *Bridge-EP */
  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP1_ENERGY_STATUS);
      rapl_pp1_before[rapl_socket] = (double)result * rapl_energy_units;
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP1_POLICY);
      rapl_pp1_policy = (int)result&0x001f;
    }
  else 
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_before[rapl_socket] = (double)result * rapl_energy_units;
    }
  rapl_start_ts[rapl_socket] = rapl_read_getticks();
}



void
rapl_read_stop()
{
   if (!rapl_allowed())
    {
      return;
    }

  rapl_stop_ts[rapl_socket] = rapl_read_getticks();
  long long int result; 

  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);  
  rapl_package_after[rapl_socket] = (double)result * rapl_energy_units;
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_after[rapl_socket] = (double)result * rapl_energy_units;

  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP1_ENERGY_STATUS);
      rapl_pp1_after[rapl_socket] = (double)result * rapl_energy_units;
    }
  else 
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_after[rapl_socket] = (double)result * rapl_energy_units;
    }
}


void
rapl_read_start_pack_pp0()
{
  if (!rapl_allowed())
    {
      return;
    }

  rapl_start_ts[rapl_socket] = rapl_read_getticks();
  long long int result; 
  if (rapl_dram_counter)
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_before[rapl_socket] = (double)result;
    }
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);
  rapl_package_before[rapl_socket] = (double)result;
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_before[rapl_socket] = (double)result;
}

void
rapl_read_stop_pack_pp0()
{
  if (!rapl_allowed())
    {
      return;
    }

  long long int result; 
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_after[rapl_socket] = (double)result;
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);  
  rapl_package_after[rapl_socket] = (double)result;
  if (rapl_dram_counter)
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_after[rapl_socket] = (double)result;
    }
  rapl_stop_ts[rapl_socket] = rapl_read_getticks();

  rapl_package_before[rapl_socket] *= rapl_energy_units;
  rapl_package_after[rapl_socket] *= rapl_energy_units;
  rapl_pp0_before[rapl_socket] *= rapl_energy_units;
  rapl_pp0_after[rapl_socket] *= rapl_energy_units;
  if(rapl_dram_counter)
    {
      rapl_dram_before[rapl_socket] *= rapl_energy_units;
      rapl_dram_after[rapl_socket] *= rapl_energy_units;
    }
}

void
rapl_read_start_pack_pp0_unprotected()
{
  rapl_start_ts[rapl_socket] = rapl_read_getticks();
  long long int result; 
  if (rapl_dram_counter)
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_before[rapl_socket] = (double)result;
    }
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);
  rapl_package_before[rapl_socket] = (double)result;
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_before[rapl_socket] = (double)result;
}

void
rapl_read_stop_pack_pp0_unprotected()
{
  long long int result; 
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PP0_ENERGY_STATUS);
  rapl_pp0_after[rapl_socket] = (double)result;
  result = read_msr(rapl_msr_fd[rapl_socket], MSR_PKG_ENERGY_STATUS);  
  rapl_package_after[rapl_socket] = (double)result;
  if (rapl_dram_counter)
    {
      result = read_msr(rapl_msr_fd[rapl_socket], MSR_DRAM_ENERGY_STATUS);
      rapl_dram_after[rapl_socket] = (double)result;
    }
  rapl_stop_ts[rapl_socket] = rapl_read_getticks();

  rapl_package_before[rapl_socket] *= rapl_energy_units;
  rapl_package_after[rapl_socket] *= rapl_energy_units;
  rapl_pp0_before[rapl_socket] *= rapl_energy_units;
  rapl_pp0_after[rapl_socket] *= rapl_energy_units;
  if(rapl_dram_counter)
    {
      rapl_dram_before[rapl_socket] *= rapl_energy_units;
      rapl_dram_after[rapl_socket] *= rapl_energy_units;
    }
}


void
rapl_read_start_pack_pp0_unprotected_all()
{
  rapl_start_ts[0] = rapl_read_getticks();
  int i;
  for (i = 1; i < NUMBER_OF_SOCKETS; i++)
    {
      rapl_start_ts[i] = rapl_start_ts[0];
    }

  FOR_ALL_SOCKETS(i)
  {
    long long int result; 
    if (rapl_dram_counter)
      {
	result = read_msr(rapl_msr_fd[i], MSR_DRAM_ENERGY_STATUS);
	rapl_dram_before[i] = (double)result;
      }
    result = read_msr(rapl_msr_fd[i], MSR_PKG_ENERGY_STATUS);
    rapl_package_before[i] = (double)result;
    result = read_msr(rapl_msr_fd[i], MSR_PP0_ENERGY_STATUS);
    rapl_pp0_before[i] = (double)result;
  }
}

void
rapl_read_stop_pack_pp0_unprotected_all()
{
  int i;
  FOR_ALL_SOCKETS(i)
  {
    long long int result; 
    result = read_msr(rapl_msr_fd[i], MSR_PP0_ENERGY_STATUS);
    rapl_pp0_after[i] = (double)result;
    result = read_msr(rapl_msr_fd[i], MSR_PKG_ENERGY_STATUS);  
    rapl_package_after[i] = (double)result;
    if (rapl_dram_counter)
      {
	result = read_msr(rapl_msr_fd[i], MSR_DRAM_ENERGY_STATUS);
	rapl_dram_after[i] = (double)result;
      }
  }

  rapl_stop_ts[0] = rapl_read_getticks();
  for (i = 1; i < NUMBER_OF_SOCKETS; i++)
    {
      rapl_stop_ts[i] = rapl_stop_ts[0];
    }

  FOR_ALL_SOCKETS(i)
  {
    rapl_package_before[i] *= rapl_energy_units;
    rapl_package_after[i] *= rapl_energy_units;
    rapl_pp0_before[i] *= rapl_energy_units;
    rapl_pp0_after[i] *= rapl_energy_units;
    if(rapl_dram_counter)
      {
	rapl_dram_before[i] *= rapl_energy_units;
	rapl_dram_after[i] *= rapl_energy_units;
      }
  }
}

void
rapl_read_print(int detailed)
{
  if (!rapl_allowed())
    {
      return;
    }

  if (rapl_package_after[rapl_socket] < rapl_package_before[rapl_socket])
    {
      printf("[RAPL] WARNING: the measurements might have overflown!\n");
    }

  if (detailed > RAPL_PRINT_NOT)
    {
      if (detailed >= RAPL_PRINT_ALL)
	{
	  printf("[RAPL] Power units                         : %.3f W\n", rapl_power_units);
	  printf("[RAPL] Energy units                        : %.8f J\n", rapl_energy_units);
	  printf("[RAPL] Time units                          : %.8f s\n", rapl_time_units);
	  printf("[RAPL] PowerPlane0 core %2d policy          : %d\n", rapl_core, rapl_pp0_policy);
	}

      if (detailed >= RAPL_PRINT_ALL)
	{
	  printf("[RAPL] Package thermal spec                : %.3f W\n", rapl_thermal_spec_power);
	  printf("[RAPL] Package minimum power               : %.3f W\n", rapl_minimum_power);
	  printf("[RAPL] Package maximum power               : %.3f W\n", rapl_maximum_power);
	  printf("[RAPL] Package maximum time window         : %.6f s\n", rapl_time_window);
	  long long int result = rapl_msr_pkg_settings;
	  printf("[RAPL] Package power limits are            : %s\n",  (result >> 63) ? "locked" : "unlocked");
	  printf("[RAPL] Package power limit #1              : %.3f W for %.6f s (%s, %s)\n", 
		 rapl_pkg_power_limit_1, rapl_pkg_time_window_1,
		 (result & (1LL<<15)) ? "enabled" : "disabled",
		 (result & (1LL<<16)) ? "clamped" : "not_clamped");
	  printf("[RAPL] Package power limit #2              : %.3f W for %.6fs (%s, %s)\n", 
		 rapl_pkg_power_limit_2, rapl_pkg_time_window_2,
		 (result & (1LL<<47)) ? "enabled" : "disabled",
		 (result & (1LL<<48)) ? "clamped" : "not_clamped");

	  if ((rapl_cpu_model == CPU_SANDYBRIDGE_EP) || (rapl_cpu_model == CPU_IVYBRIDGE_EP))
	    {
	      printf("[RAPL] Accumulated Package Throttled Time  : %.6fs\n", rapl_acc_pkg_throttled_time);
	      printf("[RAPL] PowerPlane0 (core) Accumulated Throttled Time : %.6fs\n", rapl_acc_rapl_pp0_throttled_time);
	    }
	}
  
      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  printf("[RAPL] BEFORE Package energy               : %.6f J\n", rapl_package_before[rapl_socket]);
	  printf("[RAPL] BEFORE] PowerPlane0 core %2d energy   : %.6f J\n", rapl_core, rapl_pp0_before[rapl_socket]);
	}


      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  /* not available on *Bridge-EP */
	  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
	    {
	      if (rapl_pp1_before[rapl_socket] > 0)
		{
		  printf("[RAPL] PowerPlane1 (on-core GPU) before[rapl_socket]: %.6fJ\n", rapl_pp1_before[rapl_socket]);
		  printf("[RAPL] PowerPlane1 (on-core GPU) %d policy: %d\n", rapl_core, rapl_pp1_policy);
		}
	    }
	  else 
	    {
	      printf("[RAPL] DRAM energy before[rapl_socket]: %.6fJ\n", rapl_dram_before[rapl_socket]);
	    }
	}

      rapl_read_ticks duration = rapl_stop_ts[rapl_socket] - rapl_start_ts[rapl_socket];
      double duration_s = (double) duration / ((CORE_SPEED_GHZ) * 1e9);
      if (detailed >= RAPL_PRINT_ENE)
	{
	  printf("[RAPL] Duration                            : %f s\n", duration_s);
	}

      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  printf("[RAPL] AFTER Package energy                : %.6f J\n", rapl_package_after[rapl_socket]);
	  printf("[RAPL] AFTER PowerPlane0 core %2d energy    : %.6f J\n", rapl_core, rapl_pp0_after[rapl_socket]);
	}

      double rapl_package = rapl_package_after[rapl_socket] - rapl_package_before[rapl_socket];
      double rapl_pp0 = rapl_pp0_after[rapl_socket] - rapl_pp0_before[rapl_socket];
      double rapl_dram = rapl_dram_after[rapl_socket] - rapl_dram_before[rapl_socket];
      double rapl_rest = rapl_package - rapl_pp0;
      if (detailed >= RAPL_PRINT_ENE)
	{
	  printf("[RAPL] CONSUMED Total energy               : %9.6f J\n", rapl_package + rapl_dram);
	  printf("[RAPL] CONSUMED Package energy             : %9.6f J\n", rapl_package);
	  printf("[RAPL] CONSUMED PowerPlane0 core %2d energy : %9.6f J\n", rapl_core, rapl_pp0);
	  if (rapl_dram_counter)
	    {
	      printf("[RAPL] CONSUMED DRAM energy                : %9.6f J\n", rapl_dram);
	    }
	  printf("[RAPL] CONSUMED Rest energy                : %9.6f J\n", rapl_rest);
	}

      printf("[RAPL] Total power                         : %9.6f W\n", (rapl_package + rapl_dram) / duration_s);
      printf("[RAPL] Package power                       : %9.6f W\n", rapl_package / duration_s);
      printf("[RAPL] PowerPlane0 core %2d power           : %9.6f W\n", rapl_core, rapl_pp0 / duration_s);
      printf("[RAPL] DRAM power                          : %9.6f W\n", rapl_dram / duration_s);
      printf("[RAPL] Rest power                          : %9.6f W\n", rapl_rest / duration_s);

      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  /* not available on SandyBridge-EP */
	  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
	    {
	      if (rapl_pp1_after[rapl_socket] > 0)
		{
		  printf("[RAPL] PowerPlane1 (on-core GPU) after[rapl_socket]: %.6f  (%.6fJ consumed)\n", 
			 rapl_pp1_after[rapl_socket], rapl_pp1_after[rapl_socket] - rapl_pp1_before[rapl_socket]);
		}
	    }
	  else 
	    {
	      printf("[RAPL] DRAM energy after[rapl_socket]: %.6f  (%.6fJ consumed)\n", rapl_dram_after[rapl_socket], rapl_dram_after[rapl_socket]-rapl_dram_before[rapl_socket]);
	    }
	}
    }
}


#define FOR_ALL_SOCKETS_PRINT(pattern, var, div_sum, fin)	\
  {								\
    double ___sum = 0;						\
    int ___s;							\
    for (___s = 0; ___s < NUMBER_OF_SOCKETS; ___s++)		\
      {								\
	if (rapl_initialized[___s])				\
	  {							\
	    ___sum += var[___s];				\
	  }							\
      }								\
    printf(pattern, ___sum / div_sum);				\
    for (___s = 0; ___s < NUMBER_OF_SOCKETS; ___s++)		\
      {								\
	if (rapl_initialized[___s])				\
	  {							\
	    printf(pattern, var[___s]);				\
	  }							\
	else							\
	  {							\
	    printf(pattern, 0.0);				\
	  }							\
      }								\
    printf(fin);						\
  }


void
rapl_read_print_all_sockets(int detailed, int protected)
{
  if (protected && !rapl_allowed_once())
    {
      return;
    }
  int s;

  FOR_ALL_SOCKETS(s)
  {
    if (rapl_initialized[s] && rapl_package_after[s] < rapl_package_before[s])
      {
	printf("[RAPL][%d] WARNING: the measurements might have overflown!\n", s);
      }
  }

  printf("[RAPL]                                     : ");
  printf("%-12s", "Total");
  FOR_ALL_SOCKETS(s)
  {
    printf("Socket %-4d ", s);
  }
  printf("\n");

  if (detailed > RAPL_PRINT_NOT)
    {
      if (detailed >= RAPL_PRINT_ALL)
	{
	  printf("[RAPL] Power units                         : %.3f W\n", rapl_power_units);
	  printf("[RAPL] Energy units                        : %.8f J\n", rapl_energy_units);
	  printf("[RAPL] Time units                          : %.8f s\n", rapl_time_units);
	  printf("[RAPL] PowerPlane0 core %2d policy          : %d\n", rapl_core, rapl_pp0_policy);
	}

      if (detailed >= RAPL_PRINT_ALL)
	{
	  printf("[RAPL] Package thermal spec                : %.3f W\n", rapl_thermal_spec_power);
	  printf("[RAPL] Package minimum power               : %.3f W\n", rapl_minimum_power);
	  printf("[RAPL] Package maximum power               : %.3f W\n", rapl_maximum_power);
	  printf("[RAPL] Package maximum time window         : %.6f s\n", rapl_time_window);
	  long long int result = rapl_msr_pkg_settings;
	  printf("[RAPL] Package power limits are            : %s\n",  (result >> 63) ? "locked" : "unlocked");
	  printf("[RAPL] Package power limit #1              : %.3f W for %.6f s (%s, %s)\n", 
		 rapl_pkg_power_limit_1, rapl_pkg_time_window_1,
		 (result & (1LL<<15)) ? "enabled" : "disabled",
		 (result & (1LL<<16)) ? "clamped" : "not_clamped");
	  printf("[RAPL] Package power limit #2              : %.3f W for %.6fs (%s, %s)\n", 
		 rapl_pkg_power_limit_2, rapl_pkg_time_window_2,
		 (result & (1LL<<47)) ? "enabled" : "disabled",
		 (result & (1LL<<48)) ? "clamped" : "not_clamped");

	  if ((rapl_cpu_model == CPU_SANDYBRIDGE_EP) || (rapl_cpu_model == CPU_IVYBRIDGE_EP))
	    {
	      printf("[RAPL] Accumulated Package Throttled Time  : %.6fs\n", rapl_acc_pkg_throttled_time);
	      printf("[RAPL] PowerPlane0 (core) Accumulated Throttled Time : %.6fs\n", rapl_acc_rapl_pp0_throttled_time);
	    }
	}
  
      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  printf("[RAPL] BEFORE Package energy               : %.6f J\n", rapl_package_before[rapl_socket]);
	  printf("[RAPL] BEFORE PowerPlane0 core %2d energy   : %.6f J\n", rapl_core, rapl_pp0_before[rapl_socket]);
	}


      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  /* not available on *Bridge-EP */
	  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
	    {
	      if (rapl_pp1_before[rapl_socket] > 0)
		{
		  printf("[RAPL] PowerPlane1 (on-core GPU) before[rapl_socket]: %.6fJ\n", rapl_pp1_before[rapl_socket]);
		  printf("[RAPL] PowerPlane1 (on-core GPU) %d policy: %d\n", rapl_core, rapl_pp1_policy);
		}
	    }
	  else 
	    {
	      printf("[RAPL] DRAM energy before[rapl_socket]: %.6fJ\n", rapl_dram_before[rapl_socket]);
	    }
	}

      rapl_read_ticks duration[NUMBER_OF_SOCKETS]; 
      double duration_s[NUMBER_OF_SOCKETS];
      FOR_ALL_SOCKETS(s)
      {
	duration[s] = rapl_stop_ts[s] - rapl_start_ts[s];
	duration_s[s] = (double) duration[s] / ((CORE_SPEED_GHZ) * 1e9);
      }

      if (detailed >= RAPL_PRINT_ENE)
	{
	  printf("[RAPL] Duration                            : ");
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", duration_s, rapl_num_active_sockets, " s\n");
	}

      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  printf("[RAPL] AFTER Package energy                : %.6f J\n", rapl_package_after[rapl_socket]);
	  printf("[RAPL] AFTER PowerPlane0 core %2d energy    : %.6f J\n", rapl_core, rapl_pp0_after[rapl_socket]);
	}

      double rapl_total[NUMBER_OF_SOCKETS];
      double rapl_package[NUMBER_OF_SOCKETS];
      double rapl_pp0[NUMBER_OF_SOCKETS]; 
      double rapl_dram[NUMBER_OF_SOCKETS]; 
      double rapl_rest[NUMBER_OF_SOCKETS]; 

      FOR_ALL_SOCKETS(s)
      {
	rapl_package[s] = rapl_package_after[s] - rapl_package_before[s];
	rapl_pp0[s] = rapl_pp0_after[s] - rapl_pp0_before[s];
	rapl_dram[s] = rapl_dram_after[s] - rapl_dram_before[s];
	rapl_rest[s] = rapl_package[s] - rapl_pp0[s];
	rapl_total[s] = rapl_package[s] + rapl_dram[s];
      }

      if (detailed >= RAPL_PRINT_ENE)
	{
	  printf("[RAPL] CONSUMED Total energy               : ");
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_total, 1, " J\n");
	  printf("[RAPL] CONSUMED Package energy             : ");
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_package, 1, " J\n");
	  printf("[RAPL] CONSUMED PowerPlane0 energy         : ");
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_pp0, 1, " J\n");
	  if (rapl_dram_counter)
	    {
	      printf("[RAPL] CONSUMED DRAM energy                : ");
	      FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_dram, 1, " J\n");
	    }
	  printf("[RAPL] CONSUMED Rest energy                : " );
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_rest, 1, " J\n");
	}

      double rapl_total_pow[NUMBER_OF_SOCKETS];
      double rapl_package_pow[NUMBER_OF_SOCKETS];
      double rapl_pp0_pow[NUMBER_OF_SOCKETS]; 
      double rapl_dram_pow[NUMBER_OF_SOCKETS]; 
      double rapl_rest_pow[NUMBER_OF_SOCKETS]; 

      FOR_ALL_SOCKETS(s)
      {
	rapl_package_pow[s] = rapl_package[s] / duration_s[s];
	rapl_pp0_pow[s] = rapl_pp0[s] / duration_s[s];
	rapl_dram_pow[s] = rapl_dram[s] / duration_s[s];		
	rapl_rest_pow[s] = rapl_rest[s] / duration_s[s];		
	rapl_total_pow[s] = rapl_total[s] / duration_s[s];
      }

      printf("[RAPL] CONSUMED Total power                : ");
      FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_total_pow, 1, " W\n");
      printf("[RAPL] CONSUMED Package power              : ");
      FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_package_pow, 1, " W\n");
      printf("[RAPL] CONSUMED PowerPlane0 power          : ");
      FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_pp0_pow, 1, " W\n");
      if (rapl_dram_counter)
	{
	  printf("[RAPL] CONSUMED DRAM power                 : ");
	  FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_dram_pow, 1, " W\n");
	}
      printf("[RAPL] CONSUMED Rest power                 : " );
      FOR_ALL_SOCKETS_PRINT("%11.6f ", rapl_rest_pow, 1, " W\n");


      if (detailed >= RAPL_PRINT_BEF_AFT)
	{
	  /* not available on SandyBridge-EP */
	  if ((rapl_cpu_model == CPU_SANDYBRIDGE) || (rapl_cpu_model == CPU_IVYBRIDGE) || (rapl_cpu_model == CPU_HASWELL)) 
	    {
	      if (rapl_pp1_after[rapl_socket] > 0)
		{
		  printf("[RAPL] PowerPlane1 (on-core GPU) after[rapl_socket]: %.6f  (%.6fJ consumed)\n", 
			 rapl_pp1_after[rapl_socket], rapl_pp1_after[rapl_socket] - rapl_pp1_before[rapl_socket]);
		}
	    }
	  else 
	    {
	      printf("[RAPL] DRAM energy after[rapl_socket]: %.6f  (%.6fJ consumed)\n", rapl_dram_after[rapl_socket], rapl_dram_after[rapl_socket]-rapl_dram_before[rapl_socket]);
	    }
	}
    }
}

void
rapl_read_term()
{
  if (!rapl_allowed())
    {
      return;
    }

  close(rapl_msr_fd[rapl_socket]);
}


#define FOR_ALL_SOCKETS_SUM(p_in, p_out)		\
  {							\
    double ___sum = 0;					\
    int ___s;						\
    for (___s = 0; ___s < NUMBER_OF_SOCKETS; ___s++)	\
      {							\
	___sum += p_in[___s];				\
	p_out[___s] = p_in[___s];			\
      }							\
    p_out[___s] = ___sum;				\
  }						

#define FOR_ALL_SOCKETS_PLUS1(s)		\
    for (s = 0; s < NUMBER_OF_SOCKETS + 1; s++)

void
rapl_read_stats(rapl_stats_t* s)
{
  rapl_read_ticks duration[NUMBER_OF_SOCKETS];
  double duration_s[NUMBER_OF_SOCKETS];
  double rapl_package[NUMBER_OF_SOCKETS];
  double rapl_pp0[NUMBER_OF_SOCKETS];
  double rapl_rest[NUMBER_OF_SOCKETS];
  double rapl_dram[NUMBER_OF_SOCKETS];

  int i;
  FOR_ALL_SOCKETS(i)
  {
    duration[i] = rapl_stop_ts[i] - rapl_start_ts[i];
    duration_s[i] = (double) duration[i] / ((CORE_SPEED_GHZ) * 1e9);
    rapl_package[i] = rapl_package_after[i] - rapl_package_before[i];
    rapl_pp0[i] = rapl_pp0_after[i] - rapl_pp0_before[i];
    rapl_rest[i] = rapl_package[i] - rapl_pp0[i];
    if (rapl_dram_counter)
      {
	rapl_dram[i] = rapl_dram_after[i] - rapl_dram_before[i];
      }
    else
      {
	rapl_dram[i] = 0;
      }
  }
  
  FOR_ALL_SOCKETS_SUM(duration_s, s->duration);
  s->duration[NUMBER_OF_SOCKETS] /= rapl_num_active_sockets;
  FOR_ALL_SOCKETS_SUM(rapl_package, s->energy_package);
  FOR_ALL_SOCKETS_SUM(rapl_pp0, s->energy_pp0);
  FOR_ALL_SOCKETS_SUM(rapl_rest, s->energy_rest);
  FOR_ALL_SOCKETS_SUM(rapl_dram, s->energy_dram);
  FOR_ALL_SOCKETS_PLUS1(i)
  {
    s->energy_total[i] = s->energy_package[i] + s->energy_dram[i];
  }

  if (duration_s > 0)
    {
      FOR_ALL_SOCKETS_PLUS1(i)
      {
	s->power_package[i] = s->energy_package[i] / s->duration[i];
	s->power_pp0[i] = s->energy_pp0[i] / s->duration[i];
	s->power_rest[i] = s->energy_rest[i] / s->duration[i];
	s->power_dram[i] = s->energy_dram[i] / s->duration[i];
	s->power_total[i] = s->energy_total[i] / s->duration[i];
      }

    }
}
