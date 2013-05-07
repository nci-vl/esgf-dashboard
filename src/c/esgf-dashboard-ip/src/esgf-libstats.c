#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include "libpq-fe.h"

#include "../include/stats.h"
#include "../include/dbAccess.h"
#include "../include/debug.h"
#include "../include/config.h"

#define CONNECTION_STRING "host=%s port=%d dbname=%s user=%s password=%s"

int display_sensor_structures_info(int num_sensors, struct sensor_struct *sens_struct)
{
   int i,j;
   for (i=0; i<num_sensors;i++)
	{
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor name [%s])\n",i,(sens_struct[i]).sensor_name);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor type [%s])\n",i,(sens_struct[i]).sensor_type);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor executable [%s])\n",i,(sens_struct[i]).sensor_executable);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor args [%s])\n",i,(sens_struct[i]).sensor_args);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"File name [%s])\n",(sens_struct[i]).file_name_sensor_stats);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Reset on start [%d]\n",(sens_struct[i]).reset_onstart);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"External sensor [%d]\n",(sens_struct[i]).ext_sensor);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Aggregation enabled [%d]\n",(sens_struct[i]).federation);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Time interval [%d]\n",(sens_struct[i]).time_interval);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Windows number [%d] \n",(sens_struct[i]).windows_number);
   	for (j=0; j<(sens_struct[i]).windows_number;j++)
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"|-> window [%d] Limit [%d]\n",j,(sens_struct[i]).windows_limits[j]);
	}	
}

void *
thread_serve (void *arg)
{
   int counter=3; // iterations per sensor		  
   struct stats_struct availability_struct; 
   struct sensor_struct *sens_struct = (struct sensor_struct *) arg;
   
   if (sens_struct->reset_onstart)
	remove(sens_struct->file_name_sensor_stats);
    
   reset_sensor_struct_and_raw_stats_file(sens_struct);

   setup_and_synchronize_start_time(sens_struct);

   setup_time_windows(sens_struct);

   create_metric_stat_table(sens_struct);

   //while (1) TEST_ 
   while (counter--) 
    {
    increment_num_interval(sens_struct);
	
    compute_and_display_current_time_stamp(sens_struct);
	
    produce_and_append_next_sample_to_raw_file(&availability_struct,sens_struct);
     
    shift_windows_set(&availability_struct,sens_struct); 

    store_metrics_into_tmp_stats_table(sens_struct);

    if (sens_struct->federation)
    	federating_aggregated_sensor_stats(sens_struct);
     
    //rename_tmp_stats_table_into_final_table(sens_struct); 
    	  
    sleep(sens_struct->time_interval);
    }
    
   display_windows_pointers(sens_struct);
   close_windows_FILE_pointers(sens_struct);

 return 0;
}

int sensor_specific_thread()
{
   return 0;
}

int increment_num_interval(struct sensor_struct *sens_struct)
{
    // next serial timestamp 
    sens_struct->num_interval= sens_struct->num_interval + 1;	
   return 0;
}

int create_metric_stat_table(struct sensor_struct *sens_struct)
{
   char query_create_table[2048];
   snprintf(query_create_table,sizeof(query_create_table),QUERY_CREATE_METRIC_TABLE,sens_struct->sensor_name,sens_struct->sensor_name); 
   if (transaction_based_query(query_create_table, QUERY8, QUERY4))
   	pmesg(LOG_WARNING,__FILE__,__LINE__,"Table for sensor %s already existing\n",sens_struct->sensor_name);
   pmesg(LOG_DEBUG,__FILE__,__LINE__,"New table for sensor %s ok!\n",sens_struct->sensor_name);
   //display_windows_metrics(&sens_struct);
   return 0;
}

int compute_and_display_current_time_stamp(struct sensor_struct *sens_struct)
{
    char* c_time_string;

    sens_struct->current_time = sens_struct->current_time + sens_struct->time_interval;
    c_time_string = ctime(&(sens_struct->current_time));
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"***************************************************\n");
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"Regular current time for stats is %s", c_time_string);
   return 0;
}

int close_windows_FILE_pointers(struct sensor_struct *sens_struct)
{
   int i;
   for (i=0 ; i<sens_struct->windows_number; i++) 
   	close_file(sens_struct->windows_FILE_pointer[i]);
    return 0;
}

int display_windows_pointers(struct sensor_struct *sens_struct)
{
   int i;
   for (i=0; i<sens_struct->windows_number; i++) 
    	pmesg(LOG_DEBUG,__FILE__,__LINE__,"|C-> [%d] Window pointer [%lld]\n",i,sens_struct->windows_pointers[i]);
   pmesg(LOG_DEBUG,__FILE__,__LINE__,"|D-> sensor name [%s] sensor executable [%s] sensor type [%s] sensor filename [%s]\n",sens_struct->sensor_name,sens_struct->sensor_executable,sens_struct->sensor_type, sens_struct->file_name_sensor_stats);
   pmesg(LOG_DEBUG,__FILE__,__LINE__,"end process\n");
    
   return 0;
}

int shift_windows_set(struct stats_struct *availability_struct,struct sensor_struct *sens_struct)
{
    char query_stats[1024];
    int i;

    for (i=0 ; i<sens_struct->windows_number; i++)
    	if (!(sens_struct->windows_pointers[i])) // set pointer if undefined (this means it does not exist an older last5 min sample)
    		{
		fread(&(sens_struct->stats_array[i]), sizeof(struct stats_struct), 1, sens_struct->windows_FILE_pointer[i]);
		sens_struct->windows_pointers[i]=sens_struct->num_interval; // now the window exists!!!
		sens_struct->windows_length[i]++;
		sens_struct->aggregated_values[i]+=(availability_struct->metrics);	
   		sens_struct->window_avg_values_p[i]=sens_struct->aggregated_values[i]/sens_struct->windows_limits[i];
   		sens_struct->window_avg_values_o[i]=sens_struct->aggregated_values[i]/sens_struct->windows_length[i];
    		pmesg(LOG_DEBUG,__FILE__,__LINE__,"|A->i=[%d] Pointer=[%lld] Metrics=[%4.2f] Aggreg=[%4.2f] Length=[%lld] Optim=[%4.2f] Pessim=[%4.2f]\n", i,sens_struct->windows_pointers[i],availability_struct->metrics,sens_struct->aggregated_values[i],sens_struct->windows_length[i], sens_struct->window_avg_values_o[i],sens_struct->window_avg_values_p[i]);
		}
		else 
    		{ 
		 if (sens_struct->windows_length[i] == sens_struct->windows_limits[i] || ((sens_struct->num_interval-sens_struct->windows_pointers[i])==(sens_struct->windows_limits[i]))) 
    			{
			sens_struct->aggregated_values[i]-=sens_struct->stats_array[i].metrics;
			fread(&(sens_struct->stats_array[i]), sizeof(struct stats_struct), 1, sens_struct->windows_FILE_pointer[i]);
			if (sens_struct->windows_length[i] == sens_struct->windows_limits[i])   
				{
 			   	sens_struct->windows_pointers[i]++;
				pmesg(LOG_DEBUG,__FILE__,__LINE__,"Regular window\n");
				}
			else 
				if ((sens_struct->num_interval-sens_struct->windows_pointers[i])==sens_struct->windows_limits[i])
					{
					pmesg(LOG_DEBUG,__FILE__,__LINE__, "Non regular window!\n");	
					sens_struct->windows_pointers[i]=sens_struct->stats_array[i].intervals;
					}

			sens_struct->aggregated_values[i]+=(availability_struct->metrics);
   			sens_struct->window_avg_values_p[i]=sens_struct->aggregated_values[i]/sens_struct->windows_limits[i];
   			sens_struct->window_avg_values_o[i]=sens_struct->aggregated_values[i]/sens_struct->windows_length[i];
   			} 
			else 
    			{
			  sens_struct->windows_length[i]++;
			  sens_struct->aggregated_values[i]+=(availability_struct->metrics);
   			  sens_struct->window_avg_values_p[i]=sens_struct->aggregated_values[i]/sens_struct->windows_limits[i];
   			  sens_struct->window_avg_values_o[i]=sens_struct->aggregated_values[i]/sens_struct->windows_length[i];
   			}
			
    		pmesg(LOG_DEBUG,__FILE__,__LINE__,"|B->i=[%d] Pointer=[%lld] Metrics=[%4.2f] Aggreg=[%4.2f] Length=[%lld] Optim=[%4.2f] Pessim=[%4.2f]\n", i,sens_struct->windows_pointers[i],availability_struct->metrics,sens_struct->aggregated_values[i],sens_struct->windows_length[i], sens_struct->window_avg_values_o[i],sens_struct->window_avg_values_p[i]);

   		}


    return 0;
}

int rename_tmp_stats_table_into_final_table(struct sensor_struct *sens_struct)
{
    char query_stats[1024];

    snprintf(query_stats,sizeof(query_stats),QUERY_RENAME_METRIC_TABLE,sens_struct->sensor_name,sens_struct->sensor_name,sens_struct->sensor_name); 
    if (transaction_based_query(query_stats, QUERY8, QUERY4))
   	pmesg(LOG_ERROR,__FILE__,__LINE__,"Error renaming the new stats table for sensor [%s] - query [%s]\n",sens_struct->sensor_name, query_stats);
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"New stats table successfully renamed for sensor %s!\n",sens_struct->sensor_name);
    return 0;
}

int store_metrics_into_tmp_stats_table(struct sensor_struct *sens_struct)
{
    char query_insert_stats[1024];
    char query_update_stats[1024];

    snprintf(query_insert_stats,sizeof(query_insert_stats),QUERY_INSERT_METRIC_TABLE,sens_struct->sensor_name,ESGF_HOSTNAME,sens_struct->sensor_name,sens_struct->window_avg_values_o[0],sens_struct->window_avg_values_o[1],sens_struct->window_avg_values_o[2],sens_struct->window_avg_values_o[3],sens_struct->window_avg_values_o[4],sens_struct->window_avg_values_o[5],sens_struct->window_avg_values_p[0],sens_struct->window_avg_values_p[1],sens_struct->window_avg_values_p[2],sens_struct->window_avg_values_p[3],sens_struct->window_avg_values_p[4],sens_struct->window_avg_values_p[5]); 

   if (transaction_based_query(query_insert_stats, QUERY8, QUERY4))
	{
   	//pmesg(LOG_DEBUG,__FILE__,__LINE__,"Error inserting the new stats (o,p) for sensor %s  - query [%s]\n",sens_struct->sensor_name, query_insert_stats);
    	snprintf(query_update_stats,sizeof(query_update_stats),QUERY_UPDATE_METRIC_TABLE,sens_struct->sensor_name,sens_struct->window_avg_values_o[0],sens_struct->window_avg_values_o[1],sens_struct->window_avg_values_o[2],sens_struct->window_avg_values_o[3],sens_struct->window_avg_values_o[4],sens_struct->window_avg_values_o[5],sens_struct->window_avg_values_p[0],sens_struct->window_avg_values_p[1],sens_struct->window_avg_values_p[2],sens_struct->window_avg_values_p[3],sens_struct->window_avg_values_p[4],sens_struct->window_avg_values_p[5],ESGF_HOSTNAME,sens_struct->sensor_name); 
   	if (transaction_based_query(query_update_stats, QUERY8, QUERY4))
   		pmesg(LOG_ERROR,__FILE__,__LINE__,"Error adding & updating the new stats (o,p) for sensor %s  - query [%s]\n",sens_struct->sensor_name, query_update_stats);
	else
   		pmesg(LOG_DEBUG,__FILE__,__LINE__,"New stats (o,p) for sensor %s successfully updated!\n",sens_struct->sensor_name);
	} 
	else
   	pmesg(LOG_DEBUG,__FILE__,__LINE__,"New stats (o,p) for sensor %s successfully added!\n",sens_struct->sensor_name);

   return 0;
}

int display_windows_metrics(struct sensor_struct *sens_struct)
{ 
   int i=0;

   for (i=0; i<sens_struct->windows_number; i++) 
    	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Last [%d] minutes window pointer [%d]\n",i,sens_struct->windows_pointers[i]);
   for (i=0 ; i<sens_struct->windows_number; i++) // to be replaced with the line above (7 instead of 3)
	if (sens_struct->windows_length[i])
   		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Average_o[%d] on [%d] values = [%4.2f]\n", i,sens_struct->windows_length[i], sens_struct->window_avg_values_o[i]);
	else
   		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Average_o[%d] is undefined\n",i);
   for (i=0 ; i<sens_struct->windows_number; i++) // to be replaced with the line above (7 instead of 3)
   		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Average_p[%d]  = %4.2f\n", i,sens_struct->window_avg_values_p[i]);
    return 0;
}

int create_raw_file_if_not_existing(char* filename)
{
    FILE *binaryFile;
    if (open_create_file(&binaryFile , filename,"r")) // r  - open for reading 
        open_create_file(&binaryFile ,filename, "w+");
    close_file(binaryFile);
    return 0;
}

// setting up pointers	
int setup_time_windows(struct sensor_struct *sens_struct)
{
    FILE *binaryFile;
    long long int time_counter;
    struct stats_struct stats;	
    int i;

    open_create_file(&binaryFile , sens_struct->file_name_sensor_stats,"r"); 
    time_counter=0;
    while (1)
    	{
    	if (!fread(&stats, sizeof(struct stats_struct), 1, binaryFile))
		break;
	time_counter = stats.intervals;
    	for (i=0; i<sens_struct->windows_number; i++) 
		if (time_counter<=(sens_struct->num_interval+1) &&  time_counter>=(sens_struct->num_interval+1-sens_struct->windows_limits[i]+1))
    			{
    			//fprintf(stdout,"Last [%d] minutes event [%d]\n",i,time_counter);
			if (!sens_struct->windows_pointers[i]) // set pointer if undefined
				{
				sens_struct->windows_pointers[i]=time_counter;
				fread(&(sens_struct->stats_array[i]), sizeof(struct stats_struct), 1, sens_struct->windows_FILE_pointer[i]);
   				}
			sens_struct->aggregated_values[i]+=stats.metrics;
			sens_struct->windows_length[i]++;
   			}
			else
				fread(&(sens_struct->stats_array[i]), sizeof(struct stats_struct), 1, sens_struct->windows_FILE_pointer[i]);
   	}
   close_file(binaryFile);

   if (time_counter)	
   	for (i=0 ; i<sens_struct->windows_number; i++) 
    		pmesg(LOG_DEBUG,__FILE__,__LINE__,"|A->i=[%d] FilePointer=[%lld] WinPointer=[%lld]\n", i,sens_struct->stats_array[i].intervals,sens_struct->windows_pointers[i]);
    	
   for (i=0 ; i<sens_struct->windows_number; i++) 
   	sens_struct->window_avg_values_p[i]=sens_struct->aggregated_values[i]/sens_struct->windows_limits[i];
   for (i=0 ; i<sens_struct->windows_number; i++) 
	if (sens_struct->windows_length[i])
   		sens_struct->window_avg_values_o[i]=sens_struct->aggregated_values[i]/sens_struct->windows_length[i];
	else
		sens_struct->window_avg_values_o[i]=-1; // -1 means undefined
    return 0;
}

// setup start time and synchronize current_time 
int setup_and_synchronize_start_time(struct sensor_struct *sens_struct)
{
    FILE *binaryFile;
    struct start_stats_struct start_time_struct;	
    double diff_time;
    char* c_string;
    char* c2_time_string;
    char file_name_start_stats[1024];

    // ***** START setup start time only at the beginning of the information provider ******
    snprintf(file_name_start_stats,sizeof(file_name_start_stats),FILE_NAME_START_STATS,DASHBOARD_SERVICE_PATH); 
    
    if (open_create_file(&binaryFile , file_name_start_stats,"r")) // r  - open for reading 
    {
    	time(&(start_time_struct.start_time));
    	time(&(sens_struct->current_time));
        open_create_file(&binaryFile , file_name_start_stats,"w+");
    	write_start_time_to_file(binaryFile , &start_time_struct);
    	close_file(binaryFile);
    	sens_struct->num_interval = 0;
        diff_time = 0; // by default in this branch start_time=current_time 
    } 
    else
    {
      // starting time file already exists! Read the start time value
      read_start_from_file(binaryFile,&start_time_struct);
      close_file(binaryFile);
      time(&(sens_struct->current_time));
      sens_struct->num_interval=(long long int) ((sens_struct->current_time-start_time_struct.start_time)/sens_struct->time_interval);
      diff_time = difftime(sens_struct->current_time,start_time_struct.start_time);
    } 
    
    c_string = ctime(&(start_time_struct.start_time));
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"Starting time for stats is: %s Number of past T[i] (time intervals) [%lld]\n", c_string,sens_struct->num_interval);
    sens_struct->current_time = sens_struct->current_time - sens_struct->time_interval;
    c2_time_string = ctime(&(sens_struct->current_time));
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"Raw (current time -1) for stats is: %s\n", c2_time_string);
    if ((int)diff_time % sens_struct->time_interval)
    {
	// the line below will be commented ones the sleep one will be uncommented
    	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Synchronize (%d) seconds\n", sens_struct->time_interval- ((int)diff_time % sens_struct->time_interval));	
	sleep(sens_struct->time_interval- ((int)diff_time % sens_struct->time_interval));
    	sens_struct->current_time = sens_struct->current_time + sens_struct->time_interval - ((int)diff_time % sens_struct->time_interval);
    } 
    c2_time_string = ctime(&(sens_struct->current_time));
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"Synchronized (current time -1) for stats is: %s\n", c2_time_string);
    // ***** END setup start time only at the beginning of the information provider ******
    return 0; 
} 

// initializing pointers
int reset_sensor_struct_and_raw_stats_file(struct sensor_struct *sens_struct)
{
    int i;
    // creates the raw metrics file if not existing 
    create_raw_file_if_not_existing(sens_struct->file_name_sensor_stats);
  
    for (i=0; i<sens_struct->windows_number; i++) 
    	{
         sens_struct->windows_pointers[i]=0;  // 0 means undefined 
         sens_struct->windows_length[i]=0;  // 0 number of samples in the interval 
         sens_struct->aggregated_values[i]=0;  // 0 sum 
         sens_struct->window_avg_values_p[i]=0;  // 0 average 
         sens_struct->window_avg_values_o[i]=0;  // 0 average 
    	 open_create_file(&(sens_struct->windows_FILE_pointer[i]), sens_struct->file_name_sensor_stats,"r"); 
   	}
    return 0; 
} 

// todo: dovrebbe restituire il numero di sensori rilevati dal file di configurazione 
int setup_sens_struct_from_config_file(struct sensor_struct *sens_struct)
{
    

    // TEST_
    //snprintf(query_memory_metric_insert,sizeof(query_memory_metric_insert),STORE_MEMORY_METRICS,fram,uram,fswap,uswap);
    //snprintf(sens_struct->sensor_name,sizeof(sens_struct->sensor_name),"availability"); 
    //snprintf(sens_struct->sensor_executable,sizeof(sens_struct->sensor_executable),"executable availability"); 
    //snprintf(sens_struct->file_name_sensor_stats,sizeof(sens_struct->file_name_sensor_stats),FILE_NAME_STATS,sens_struct->sensor_name); 
    snprintf(sens_struct->sensor_args,sizeof(sens_struct->sensor_args), ""); 
    snprintf(sens_struct->sensor_type,sizeof(sens_struct->sensor_type), ""); 
    snprintf(sens_struct->sensor_executable,sizeof(sens_struct->sensor_executable), ""); 
    sens_struct->reset_onstart=0;			// reset raw_file removing the history every time the ip boots default 0 which means keeps the history 
    sens_struct->ext_sensor=0;  			// external sensor 1=yes, which means of out of the box sensor ; 0=no, which means core sensor 
    sens_struct->federation=0;  			// it relates to the federation mechanism. Default 0=no, 1=yes 
    sens_struct->time_interval=300;			// default 5 minutes	
    sens_struct->windows_number=6;  	// PRODUCTION_ number of time_windows to be managed 
    sens_struct->windows_limits[0]=1;   // TEST_
    sens_struct->windows_limits[1]=2;   // TEST_
    sens_struct->windows_limits[2]=4;   // TEST_
    sens_struct->windows_limits[3]=8;   // TEST_
    sens_struct->windows_limits[4]=16;  // TEST_
    sens_struct->windows_limits[5]=32;  // TEST_ 
	// PRODUCTION_
    /*
    sens_struct->windows_limits[0]=1;
    sens_struct->windows_limits[1]=12;
    sens_struct->windows_limits[2]=12*24;
    sens_struct->windows_limits[3]=12*24*7;
    sens_struct->windows_limits[4]=12*24*30;
    sens_struct->windows_limits[5]=12*24*365;*/
    return 0; 
}


int display_file(FILE* file)
{
   struct stats_struct stats;
   while (1)
   {
    if (!fread(&stats, sizeof(struct stats_struct), 1, file))
	break;
    pmesg(LOG_DEBUG,__FILE__,__LINE__,"Reading file %lld=%4.2f\n",stats.intervals, stats.metrics);
   }
   return 0;
}

/*int open_create_file(FILE** file , char* filename, char* mode)
{
    *file = fopen(filename,mode);
    if ((*file) == NULL)
	{
	 fprintf(stdout,"Can't open binary file [%s] in [%s] mode\n",filename,mode);
	 return -1; 
	}
    return 0;
}*/

int read_stats_from_file(FILE* file , struct stats_struct *stats_data)
{
    if (!fread(stats_data, sizeof(struct stats_struct), 1, file))
	return -1;	
    return 0;
}

int read_start_from_file(FILE* file , struct start_stats_struct *stats_data)
{
    if (!fread(stats_data, sizeof(struct start_stats_struct), 1, file))
	return -1;
    return 0;
}

int write_start_time_to_file(FILE* file , struct start_stats_struct *stats_p )
{
    if ( fwrite( stats_p, sizeof(struct start_stats_struct), 1, file ) != 1)
	{
	 pmesg(LOG_DEBUG,__FILE__,__LINE__,"Can't write on binary output file\n");
	 return -2; 
	}
    return 0;
}

int write_stats_to_file(FILE* file , struct stats_struct *stats )
{
    if ( fwrite( stats, sizeof(struct stats_struct), 1, file ) != 1)
	{
	 pmesg(LOG_DEBUG,__FILE__,__LINE__,"Can't write on binary output file\n");
	 return -2; 
	}
    return 0;
}

/*int close_file(FILE* file)
{
    fclose( file );
    return 0;
}*/

long long int print_element_names(xmlNode * a_node)
{    
    char *num_rec;
    xmlNode *cur_node = NULL;
    long long int num_rec_d;

    
    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
            //printf("node type: Element, name: %s\n", cur_node->name);
	    if (cur_node->type == XML_ELEMENT_NODE && !strcmp(cur_node->name,"response")) 
	    	cur_node= cur_node->children;
	    if (cur_node->type == XML_ELEMENT_NODE && !strcmp(cur_node->name,"result")) 
	     	num_rec = xmlGetProp (cur_node, "numFound");	
        }
   	
  num_rec_d=atoll(num_rec); 
  free(num_rec);	
  return num_rec_d; 
}
	
long long int parse_xml_search_file(char* tmp_file)
{
  int while_end;
  long long int num_rec_d=0;
  char *position;
  char *position2;

  FILE *file = fopen (tmp_file, "r");

  if (file == NULL)             // /esg/config/infoprovider.properties not found
    return -1;

  while_end = 1;

  while (while_end)
    {
      char buffer[256] = { '\0' };
      char value_buffer[256] = { '\0' };
      int strlen_buf,j;

      if (!(fgets (buffer,256, file))) // now reading ATTRIBUTE=VALUE
        {
	  //fprintf(stdout,"end of file\n");
	  while_end = 0;
	  break;	
        }
      //fprintf(stdout,"line = %s",buffer);
      j=0;
      strlen_buf = strlen(buffer);
      while (j<strlen_buf)
        {
	  if (buffer[j]=='n')
	    if (buffer[j+1]=='u')
  	      if (buffer[j+2]=='m')
  	      	if (buffer[j+3]=='F')
  	      	   if (buffer[j+4]=='o')
  	      	      if (buffer[j+5]=='u')
  	      	      	if (buffer[j+6]=='n')
  	      	      	   if (buffer[j+7]=='d')
        			{
          			 position = strchr(&(buffer[j+7]),'"');  
          			 position2 = strchr(position+1,'"');  
				 *position2='\0';
				 //fprintf(stdout,"trovato %s\n",position+1);
  				 num_rec_d=atoll(position+1); 
				 while_end = 0; 	
			     	 break; 
        			}
	  j++;	
        }
 	

      
      /*if (position != NULL)     // the '=' has been found, which means the line is properly written as attribute=value
        {
          strcpy (value_buffer, position + 1);  
          *position = '\0';    // now value_buffer stores the VALUE 
          position = strchr(value_buffer,'\n');  
	  *position = '\0';     // now buffer stores the ATTRIBUTE       
          //fprintf(stdout,"Attribute=[%s] Value=[%s]\n",buffer,value_buffer);
	}*/
   }
  fclose (file);
  return num_rec_d; 
}

int federating_aggregated_sensor_stats(struct sensor_struct *sens_struct)
{
	PGconn *conn;
	PGresult *res;
	char conninfo[1024] = {'\0'};
	long int numTuples;
	long long int t,w; // w is the action
	int ret_code, nFields;
  	char peername[512] = { '\0' };

	// OPEN CONNECTION  
 	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Federating aggregated sensor stats process START\n");

        snprintf (conninfo, sizeof (conninfo), CONNECTION_STRING, POSTGRES_HOST, POSTGRES_PORT_NUMBER,POSTGRES_DB_NAME, POSTGRES_USER,POSTGRES_PASSWD);
	conn = PQconnectdb ((const char *) conninfo);

	if (PQstatus(conn) != CONNECTION_OK)
        {
                pmesg(LOG_ERROR,__FILE__,__LINE__,"Federating aggregated sensor stats: Connection to database failed: %s\n", PQerrorMessage(conn));
		PQfinish(conn);
		return -1;
        }

	// SELECT START 

	res = PQexec(conn,QUERY_LIST_ACTIVE_HOSTS);

	if ((!res) || (PQresultStatus(res) != PGRES_TUPLES_OK))
    	{
	        pmesg(LOG_ERROR,__FILE__,__LINE__,"Federating aggregated sensor stats: SELECT host list FAILED\n");
	        PQclear(res);
		PQfinish(conn);
		return -2;
    	}

	numTuples = PQntuples(res);
	nFields = PQnfields(res);
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Number of fields/entries for the federation stats process to be contacted [%ld,%d] \n",nFields,numTuples);

	for(t = 0; t < numTuples; t ++) 
		{
		 char remove_stats_feddw[1024] = { '\0' };
  		 char update_query_timestamp_counter_aggr[1024] = { '\0' };
  		 char get_last_id_query[1024] = { '\0' };
		 long int last_id_feddw;

  		 last_id_feddw = 0;
		 snprintf (peername,sizeof (peername),"%s",PQgetvalue(res, t, 0));
 		 //pmesg(LOG_DEBUG,__FILE__,__LINE__,"Processing sensor [%s] for entry [%s]\n",sens_struct->sensor_name, peername);
		 harvest_aggregated_stats(conn,sens_struct->sensor_name, peername);
		 /*if (w==-1) {
 	 	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Action for [%s] planB = delete node stats from federationdw\n",peername);
		remove_stats_from_federation_level_dw(conn,remove_stats_feddw,update_query_timestamp_counter_aggr,START_TRANSACTION_FEDDW_PLANB,END_TRANSACTION_FEDDW_PLANB);
			}
		 if (w==0) {
 	 	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Action for [%s] planB = delete node stats from federationdw & get them again from scratch (non-transact)\n",peername);
		remove_stats_from_federation_level_dw(conn,remove_stats_feddw,update_query_timestamp_counter_aggr,START_TRANSACTION_FEDDW_PLANB,END_TRANSACTION_FEDDW_PLANB);
		harvest_stats_planB(conn,peername);
			}
		 if (w==1) {
 	 	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Action for [%s] planB = skipping node\n",peername);
			}
		*/
		}

	// CLOSE CONNECTION  
	PQclear(res);
    	PQfinish(conn);

 	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Federating aggregated sensor stats process END\n");

  return 0;
}

int harvest_aggregated_stats(PGconn *conn,char* sensor_name, char* peername)
{
  CURL *curl;
  PGresult *res;
  CURLcode curl_res;
  CURLINFO info;
  long http_code;
  double c_length;  
  FILE *tmp;
  FILE *file;
  char buffer[10024];
  char url_action[10024];
  char file_stats[1024];
  long int i;
  int right_url;  

  snprintf (url_action, sizeof (url_action),URL_AGGREGATED_STATS,peername, sensor_name,peername);
  snprintf (file_stats, sizeof (file_stats),".stats_%s_%s",sensor_name,peername);
  //pmesg(LOG_DEBUG,__FILE__,__LINE__,"Contacting [sensor=%s;hostname=%s;filestats=%s]\n",sensor_name,peername,file_stats);
  pmesg(LOG_DEBUG,__FILE__,__LINE__,"Contacting [sensor=%s;hostname=%s;urlaction=%s]\n",sensor_name,peername,url_action);

  tmp=fopen(file_stats, "w");
  if(tmp==NULL) 
	{
    	 pmesg(LOG_ERROR,__FILE__,__LINE__,"ERROR to open file .stats_<sensor_name><peername>\n");
    	 return -2; 
  	}

  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, url_action);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,  tmp);
  curl_res = curl_easy_perform(curl);
  if(curl_res) 
   	{
    	pmesg(LOG_ERROR,__FILE__,__LINE__,"ERROR contatting [%s] or downloading stats\n",peername);
  	remove(file_stats);
    	fclose(tmp);
    	curl_easy_cleanup(curl);
    	return -1;
   	}	
  fclose(tmp);
  curl_easy_cleanup(curl);

  file=fopen(file_stats, "r");

  if (file == NULL)    
       	{
  	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Error opening file\n");
    	return -1;
     	} 
  i=0;
  right_url = 1; 
  char line [ 1024 ];

  while ( (fgets ( line, sizeof line, file ) != NULL) && (right_url) ) 
       	{
  		char insert_remote_stat[10024] = { '\0' };
		i++;
		if (i==1)
        	{
		   if (strcmp(line,"METRICSSTATS\n"))
			right_url=0;	
		   continue;
        	} 
    		snprintf (insert_remote_stat, sizeof (insert_remote_stat),INSERT_AGGREGATED_STATS,sensor_name,line);
	
  		res = PQexec(conn, insert_remote_stat);

  		if ((!res) || (PQresultStatus (res) != PGRES_COMMAND_OK))
                	pmesg(LOG_ERROR,__FILE__,__LINE__,"Query insert in federated aggregated stats failed [%s]\n",insert_remote_stat);

  		PQclear(res);
       	} 
  
  fclose ( file );
  remove(file_stats);
  
 return 0;
}

int get_search_metrics(char* name, char* query)
{
  xmlDoc *doc = NULL;
  xmlNode *root_element = NULL;
  CURL *curl;
  CURLcode curl_res;
  CURLINFO info;
  long http_code;
  double c_length;
  FILE *tmp;
  FILE *file;
  char buffer[10024];
  char url_action[10024];
  char tmp_file[1024];
  long int i;
  long long int num_rec;
  int right_url;

  snprintf (url_action, sizeof (url_action),query);
  snprintf (tmp_file, sizeof (tmp_file),TEMP_SEARCH_STATS_FILE,name);

  // filename to be stats_<host>_<processed_id>.tmp

  tmp=fopen(tmp_file, "w");
  if(tmp==NULL)
        {
         //pmesg(LOG_ERROR,__FILE__,__LINE__,"ERROR to open file stats.tmp\n");
         return -2;
        }

  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, url_action);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,  tmp);
  curl_res = curl_easy_perform(curl);

  if(curl_res)
        {
        //pmesg(LOG_ERROR,__FILE__,__LINE__,"ERROR contatting [%s] or downloading stats\n",peername);
        remove(tmp_file);
        fclose(tmp);
        curl_easy_cleanup(curl);
        return -1;
        }
  fclose(tmp);
  curl_easy_cleanup(curl);

  num_rec = parse_xml_search_file(tmp_file);

  remove(tmp_file);

 return(num_rec);
}

int produce_and_append_next_sample_to_raw_file(struct stats_struct *availability_struct,struct sensor_struct *sens_struct)
    {
    // append the metric in the raw file
    // todo: lock file to be added
        FILE *binaryF;
    	
	// todo: The generate metrics should be a function with a SWITCH based on the type of sensor. Core types: availability, search, exec	
    	// generate the metrics
	
    	availability_struct->intervals=sens_struct->num_interval;
	if (!(strcmp(sens_struct->sensor_type,"search")))	
		availability_struct->metrics= (double) get_search_metrics(sens_struct->sensor_name, sens_struct->sensor_executable);
	if (!(strcmp(sens_struct->sensor_type,"availability")))	
    		availability_struct->metrics= sens_struct->num_interval; 

    	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Generated metrics sensor [%s] [%lld]-[%4.2f] \n", sens_struct->sensor_name,availability_struct->intervals, availability_struct->metrics);

    	open_create_file(&binaryF,sens_struct->file_name_sensor_stats,"a+"); // a+ - open for reading and writing (append if file exists)
    	write_stats_to_file(binaryF,availability_struct);
    	close_file(binaryF);
        return 0;
   }

/*int
thread_manager (struct sensor_struct *sens_struct, const unsigned num_sensors)
{
  const int MIN = num_sensors < THREAD_SENSOR_OPEN_MAX ? num_sensors : THREAD_SENSOR_OPEN_MAX;
  pthread_t *threads = (pthread_t *) malloc (sizeof (pthread_t) * MIN);
  unsigned h = 0;
  int res;

  while (h < num_sensors)
    {
      unsigned c;
      for (c = 0; h < num_sensors && c < MIN; h++, c++)
        pthread_create (threads + c, NULL, thread_serve, sens_struct);

      for (c = 0; c <= (h - 1) % MIN; c++)
        {
          void *ptr = NULL;
          res = pthread_join (threads[c], &ptr);
	  fprintf(stdout,"After joining the thread - res vale [%d] \n",res);
        }
    }

  free (threads);
  return 0;
}*/




int
thread_manager_start (pthread_t *threads, struct sensor_struct *sens_struct, const unsigned num_sensors)
{
//  pthread_t *threads = (pthread_t *) malloc (sizeof (pthread_t) * num_sensors);
  unsigned c = 0;
  //int res;

  for (c = 0; c < num_sensors; c++)
        pthread_create (threads + c, NULL, thread_serve, &sens_struct[c]);
	
/*  for (c = 0; c < num_sensors; c++)
        {
          void *ptr = NULL;
          res = pthread_join (threads[c], &ptr);
	  fprintf(stdout,"After joining the thread - res vale [%d] \n",res);
        }*/

//  free (threads);
  return 0;
}

int
thread_manager_stop (pthread_t *threads, struct sensor_struct *sens_struct, const unsigned num_sensors)
{
  unsigned c = 0;
  int res;

  for (c = 0; c < num_sensors; c++)
        {
          void *ptr = NULL;
          res = pthread_join (threads[c], &ptr);
	  pmesg(LOG_DEBUG,__FILE__,__LINE__,"After joining the thread - res vale [%d] \n",res);
        }
//  free (threads);
  return 0;
}

int read_sensors_list_from_file(struct sensor_struct *sens_struct)
{
  char sensor_file[256] = { '\0' };
  char *position;
  int while_end,found_sensor, curr_sensor;

  snprintf (sensor_file,sizeof(sensor_file),"/esg/config/infoprovider.properties");

  //pmesg(LOG_DEBUG,__FILE__,__LINE__,"%s\n", sensor_file);
  pmesg(LOG_DEBUG,__FILE__,__LINE__,"[START] %s\n", sensor_file);
 
  FILE *file = fopen (sensor_file, "r");

  if (file == NULL)             // /esg/config/infoprovider.properties not found
    return -1;

  while_end = 1;
  found_sensor=0;
  curr_sensor=-1;

  while (while_end)
    {
      char buffer[256] = { '\0' };
      char value_buffer[256] = { '\0' };

      if (!(fgets (buffer,256,file))) // now reading ATTRIBUTE=VALUE
        {
	  //fprintf(stdout,"end of file\n");
	  while_end = 0;
          fclose (file);
	  break;	
        }
      //fprintf(stdout,"line = %s\n",buffer);

      position = strchr (buffer, '=');
      if (position != NULL)     // the '=' has been found, which means the line is properly written as attribute=value
        {
          strcpy (value_buffer, position + 1);  
          *position = '\0';    // now value_buffer stores the VALUE 
          position = strchr(value_buffer,'\n');  
	  *position = '\0';     // now buffer stores the ATTRIBUTE       
	  	
          //fprintf(stdout,"Attribute=[%s] Value=[%s]\n",buffer,value_buffer);
          if (!(strcmp (buffer, "sensor"))) // IMPO mandatory: first property to be defined
            {
		curr_sensor++; // this index is the right one for this sensor
		snprintf((sens_struct[curr_sensor]).sensor_name,sizeof((sens_struct[curr_sensor]).sensor_name),value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor name [%s])\n",curr_sensor,(sens_struct[curr_sensor]).sensor_name);
   		setup_sens_struct_from_config_file(&sens_struct[curr_sensor]);

            }
          if (!(strcmp (buffer, "type"))) // mandatory
            {
		snprintf((sens_struct[curr_sensor]).sensor_type,sizeof((sens_struct[curr_sensor]).sensor_type),value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor type [%s])\n",curr_sensor,(sens_struct[curr_sensor]).sensor_type);
    		snprintf((sens_struct[curr_sensor]).file_name_sensor_stats,sizeof((sens_struct[curr_sensor]).file_name_sensor_stats),FILE_NAME_STATS,DASHBOARD_SERVICE_PATH,sens_struct[curr_sensor].sensor_type,sens_struct[curr_sensor].sensor_name); 
            }
          if (!(strcmp (buffer, "reset")))
            {
		sens_struct[curr_sensor].reset_onstart=atoi(value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor reset_onstart [%d])\n",curr_sensor,(sens_struct[curr_sensor]).reset_onstart);
            }
          if (!(strcmp (buffer, "interval")))
            {
		sens_struct[curr_sensor].time_interval=atoll(value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor time_interval [%lld])\n",curr_sensor,(sens_struct[curr_sensor]).time_interval);
            }
          if (!(strcmp (buffer, "exec")))
            {
		snprintf((sens_struct[curr_sensor]).sensor_executable,sizeof((sens_struct[curr_sensor]).sensor_executable),value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor executable [%s])\n",curr_sensor,(sens_struct[curr_sensor]).sensor_executable);
            }
          if (!(strcmp (buffer, "args")))
            {
		snprintf((sens_struct[curr_sensor]).sensor_args,sizeof((sens_struct[curr_sensor]).sensor_args),value_buffer);
		pmesg(LOG_DEBUG,__FILE__,__LINE__,"Sensor info index [%d] Sensor args [%s])\n",curr_sensor,(sens_struct[curr_sensor]).sensor_args);
            }
         }
	else {
	buffer[strlen(buffer)-1]='\0';
	pmesg(LOG_DEBUG,__FILE__,__LINE__,"Skipping line [%s]\n",buffer);
    	}

    }
  curr_sensor++; // now curr_sensor states the number of sensors. 
  return curr_sensor;
}

