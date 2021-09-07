#include "cache.h"

cache_list cache = {NULL, NULL, PTHREAD_MUTEX_INITIALIZER};

cache_list_el* cache_add(char* url, int url_len, char* host, int host_len) {
  cache_list_el* new_el = (cache_list_el*) malloc(sizeof(cache_list_el));

  char* url_copy = (char*) malloc((url_len + 1) * sizeof(char));
  memcpy(url_copy, url, url_len);
  url_copy[url_len] = '\0';
  new_el->url = url_copy;

  char* host_copy = (char*) malloc((host_len + 1) * sizeof(char));
  memcpy(host_copy, host, host_len);
  host_copy[host_len] = '\0';
  new_el->host = host_copy;

  pthread_mutex_init(&new_el->mutex, NULL);

  data_entry* new_data = (data_entry*) malloc(sizeof(data_entry));
  new_data->head = NULL;
  new_data->dirty_flag = 1;
  pthread_mutex_init(&new_data->mutex, NULL);
  new_el->data = new_data;

  new_el->next = NULL;
  new_el->prev = NULL;

  pthread_mutex_lock(&cache.mutex);
  if (!cache.tail && !cache.head) {
    cache.head = new_el;
    cache.tail = new_el;
  }
  else {
    cache.tail->next = new_el;
    new_el->prev = cache.tail;
    cache.tail = new_el;
    pthread_mutex_unlock(&cache.tail->prev->mutex);
  }
  pthread_mutex_unlock(&cache.mutex);

  return new_el;
}

cache_list_el* find_by_url_and_host(char *url, int url_len, char *host, int host_len) {
  printf("Start search node with %s %s\n", url, host);
  cache_list_el* result = NULL;
  cache_list_el* old = NULL;
  pthread_mutex_lock(&cache.mutex);
  if (!cache.head) {
    pthread_mutex_unlock(&cache.mutex);
    return result;
  }
  pthread_mutex_lock(&cache.head->mutex);
  cache_list_el* p = cache.head;
  printf("In search, got HEAD\n");
  pthread_mutex_unlock(&cache.mutex);

  int found_flag = 0;

  while (p) {
    old = p;
    if (strcmp(url, p->url) == 0 && strcmp(host, p->host) == 0) {
      printf("Found match!\nIt was %s %s\n", p->url, p->host);
      result = p;
      found_flag = 1;
    }

    if (p->next) {
      pthread_mutex_lock(&p->next->mutex);
      p = p->next;
      pthread_mutex_unlock(&old->mutex);
    }
    else {
      p = p->next;
      if (found_flag) {
        pthread_mutex_unlock(&old->mutex);
      }
    }
  }
  return result;
}

data_subentry* get_data_head(cache_list_el* el) {
  pthread_mutex_lock(&el->data->mutex);
  if (el->data->dirty_flag || !el->data) {
    printf("Thread can't get data because of dirty_flag\n");
    return NULL;
  }
  data_subentry* result = el->data->head;
  pthread_mutex_lock(&result->mutex);
  pthread_mutex_unlock(&el->data->mutex);
  return result;
}

data_subentry* init_subentry(char* data, int length) {
  data_subentry* subentry = (data_subentry*) malloc(sizeof(data_subentry));
  subentry->data = (char*) malloc((length + 1) * sizeof(char));
  memcpy(subentry->data, data, length);
  subentry->data[length] = '\0';
  subentry->length = length;
  subentry->next = NULL;
  pthread_mutex_init(&subentry->mutex, NULL);
  return subentry;
}

void set_data_head(cache_list_el* el, data_subentry* head) {
  el->data->dirty_flag = 0;
  if (!el->data) {
    free(el->data);
    el->data = NULL;
  }
  el->data->head = head;
  pthread_mutex_unlock(&el->data->mutex);
}

void abort_set(cache_list_el* el) {
  el->data->dirty_flag = 1;
  pthread_mutex_unlock(&el->data->mutex);
}
