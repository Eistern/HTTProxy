#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <malloc.h>
#include <string.h>

typedef struct data_subentry data_subentry;
typedef struct data_entry data_entry;

struct data_subentry {
  int length;
  char* data;
  data_subentry* next;
  pthread_mutex_t mutex;
};

struct data_entry {
  //PUBLIC
  data_subentry* head;
  //PRIVATE
  int dirty_flag;
  pthread_mutex_t mutex;
};

typedef struct cache_list_el cache_list_el;

struct cache_list_el {
  //READ_ONLY
  char* url;
  char* host;
  data_entry* data;
  pthread_mutex_t mutex;
  //NEXT MAY CHANGE
  cache_list_el* next;
  cache_list_el* prev;
};

typedef struct cache_list cache_list;

struct cache_list {
  //PUBLIC
  cache_list_el* head;
  cache_list_el* tail;
  //READ_ONLY
  pthread_mutex_t mutex;
};

cache_list_el* cache_add(char* url, int url_len, char* host, int host_len);

cache_list_el* find_by_url_and_host(char* url, int url_len, char* host, int host_len);

data_subentry* get_data_head(cache_list_el* el);

data_subentry* init_subentry(char* data, int length);

void set_data_head(cache_list_el* el, data_subentry *head);

void abort_set(cache_list_el* el);

#endif