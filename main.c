#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "http_parser.h"
#include "cache.h"

struct timeval timeout = {3, 0};

typedef struct {
  int ready_flag;
  char* host_name;
  int host_len;
  char* url;
  int url_len;
} host_resolve;

typedef struct {
  int chunk_flag;
} recv_chunk;

char* generate_request(char* buf, char* url, int url_len, char* host, int host_len) {
  sprintf(buf, "GET %.*s HTTP/1.1\r\nHost: %.*s\r\nConnection: close\r\n\r\n", url_len, url, host_len, host);
  return buf;
}

int on_url(http_parser* parser, const char* at, size_t length) {
  if (((host_resolve*) (parser->data))->url) {
    printf("URL field was not NULL\n");
    free(((host_resolve*) (parser->data))->url);
    ((host_resolve*) (parser->data))->url = NULL;
  }
  printf("%.*s\n", (int)length, at);
  ((host_resolve*) (parser->data))->url = (char*) malloc((length + 1) * sizeof(char));
  memcpy(((host_resolve*) (parser->data))->url, at, length * sizeof(char));
  ((host_resolve*) (parser->data))->url[length] = '\0';
  ((host_resolve*) (parser->data))->url_len = length;
  return 0;
}

int on_header_field(http_parser* parser, const char* at, size_t length) {
  if (strncmp(at, "Host", length) == 0) {
    ((host_resolve*) (parser->data))->ready_flag = 1;
  }
  else {
    ((host_resolve*) (parser->data))->ready_flag = 0;
  }
  return 0;
}

int on_header_value(http_parser* parser, const char* at, size_t length) {
  if (((host_resolve*) (parser->data))->ready_flag) {
      if (((host_resolve*) (parser->data))->host_name) {
        printf("Host name not NULL\n");
        free(((host_resolve*) (parser->data))->host_name);
        ((host_resolve*) (parser->data))->host_name = NULL;
      }
      printf("%.*s\n", (int)length, at);
      ((host_resolve*) (parser->data))->host_name = (char*) malloc((length + 1) * sizeof(char));
      memcpy(((host_resolve*) (parser->data))->host_name, at, length * sizeof(char));
      ((host_resolve*) (parser->data))->host_name[length] = '\0';
      ((host_resolve*) (parser->data))->host_len = length;
  }
  return 0;
}

int on_chunk_header(http_parser* parser) {
  ((recv_chunk*)(parser->data))->chunk_flag = 1;
  printf("Chunk length: %ld\n", parser->content_length);
  return 0;
}

int on_chunk_complete(http_parser* parser) {
  ((recv_chunk*)(parser->data))->chunk_flag = 0;
  return 0;
}

int connect_to_server(char* result_host, int result_host_len) {
  int remote_connection = -1;
  if ((remote_connection = socket(PF_INET, SOCK_STREAM, 0)) <= 0) {
    perror("Can't create socket for remote connection");
    return remote_connection;
  }

    //OPENED SOCKET TO ESATBLISH CONNECTION TO SERVER (remote_connection)
  char* protocol_found;
  if ((protocol_found = strstr(result_host, "//")) != NULL) {
    result_host = protocol_found + 2;
    result_host_len = strlen(result_host);
  }
  int remote_port = 80;

  char* delim_found = NULL;
  printf("Host found: %s\n", result_host);
  if ((delim_found = strchr(result_host, ':')) != NULL) {
    remote_port = atoi(delim_found + 1);
    delim_found[0] = '\0';
    result_host_len = delim_found - result_host;
  }

  struct hostent *hp = gethostbyname(result_host);
  char* remote_ip = NULL;
  if (hp == NULL || hp->h_addr_list[0] == NULL) {
    perror("Get host by name failed: ");
    close(remote_connection);
    remote_connection = -1;
    return remote_connection;
  }
  remote_ip = inet_ntoa(*(struct in_addr*)(hp->h_addr_list[0]));

  struct sockaddr_in remote;
  remote.sin_family = AF_INET;
  remote.sin_port = htons(remote_port);

  if(inet_pton(AF_INET, remote_ip, &remote.sin_addr)<=0) {
        perror("Inet_pton error: \n");
        close(remote_connection);
        remote_connection = -1;
        return remote_connection;
  }

  if (connect(remote_connection, (struct sockaddr*) &remote, sizeof(remote)) == -1) {
    perror("Can't connect to remote server");
    close(remote_connection);
    remote_connection = -1;
    return remote_connection;
  }

  //ESATBLISHED CONNECTION TO SERVER
  return remote_connection;
}

int send_cached_data(int home_connection, data_subentry* data_head) {
  data_subentry* data_p = data_head;
  data_subentry* data_p_next;

  int failed_send = 0;
  while (data_p) {
    if (send(home_connection, data_p->data, data_p->length, 0) == -1)
      failed_send = -1;
    data_p_next = data_p->next;
    if (data_p_next) {
      pthread_mutex_lock(&data_p_next->mutex);
    }
    pthread_mutex_unlock(&data_p->mutex);
    data_p = data_p_next;
  }
  return failed_send;
}

int transmit_data_from_server(int* remote_connection_p, int home_connection, http_parser* resp_parser, http_parser_settings* resp_settings, char *recv_h, cache_list_el* cache) {
  int remote_connection = *remote_connection_p;
  
  char* send_r = NULL;
  char buff[8196];
  buff[0] = '\0';
  char* proxy_connection = strstr(recv_h, "Proxy-Connection");
  if (proxy_connection) {
    char* end_of_header = strstr(proxy_connection, "\n");
    sprintf(buff, "%.*s%s", (int) (proxy_connection - recv_h), recv_h, end_of_header + 1);
    send_r = &buff[0];
  }
  else {
    send_r = recv_h;
  }

  if (send(remote_connection, send_r, strlen(send_r), 0) == -1) {
    perror("Can't send request to remote host, closing connection....");
    close(remote_connection);
    *remote_connection_p = -1;
    return 1;
  }

  //TRANSLATED REQUEST TO SERVER
  setsockopt(remote_connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  char* recv_r = (char*) malloc(8196 * sizeof(char));
  recv_r[0] = '\0';

  int successful_transmit = 0;

  int num = 0, need_caching = 0;
  data_subentry* previous_subentry = NULL;
  while ((num = read(remote_connection, recv_r, 8195)) > 0) {
      printf("Received %d bytes\n", num);
      //RECIEVED DATA CHUNK

      if (need_caching == 0) {
        http_parser_execute(resp_parser, resp_settings, recv_r, num);
        if (resp_parser->status_code == 200)
          need_caching = 1;
        else {
          need_caching = -1;  
          abort_set(cache);
        }
      }
      //IF IT'S THE FIRST CHUNK, PARSE RESPONSE IF status_code == 200, ENABLE CACHING

      if (send(home_connection, recv_r, num, 0) == -1) {
        perror("Error sending client....");
        successful_transmit = 1;
      }
      //TRANSFER RESPONSE CHUNK TO CLIENT (IF POSSIBLE)

      if (need_caching == 1) {
        data_subentry* next_entry = init_subentry(recv_r, num);
        pthread_mutex_lock(&next_entry->mutex);

        if (previous_subentry == NULL) {
          set_data_head(cache, next_entry);
        }
        //IF THIS IS THE FIRST data_subentry, THEN SET IT AS LOCKED HEAD
        else {
          previous_subentry->next = next_entry;
          pthread_mutex_unlock(&previous_subentry->mutex);
        }
        //IF WE HAD A previous_subentry, SET IT'S NEXT data_subentry AND UNLOCK IT
        previous_subentry = next_entry;
      }
      //IF CACHING ENABLED, 
  }

  if (need_caching == 1 && previous_subentry != NULL) {
    pthread_mutex_unlock(&previous_subentry->mutex);
  }
  //UNLOCK LAST data_subentry

  printf("LAST NUM: %d\n", num);
  if (num == 0) {
    printf("CONNECTION TO THE SERVER NEEDS TO BE CLOSED\n");
    close(remote_connection);
    *remote_connection_p = -1;
  }
  //TRANSFERED RESPONSE
  return successful_transmit;
}

void* serve_client(void* args) {
  long home_connection = (long) args;

  //GOT CONNECTION TO CLIENT (home_connection)

  http_parser_settings req_settings;
  http_parser_settings_init(&req_settings);
  req_settings.on_header_field = on_header_field;
  req_settings.on_header_value = on_header_value;
  req_settings.on_url = on_url;

  http_parser* req_parser = (http_parser*) malloc(sizeof(http_parser));
  http_parser_init(req_parser, HTTP_BOTH);

  host_resolve* resolve_data = (host_resolve*) malloc(sizeof(host_resolve));
  resolve_data->host_name = NULL;
  resolve_data->host_len = 0;
  resolve_data->url = NULL;
  resolve_data->url_len = 0;
  resolve_data->ready_flag = 0;
  req_parser->data = resolve_data;

  //REQUET PARSER INITIALIZED

  http_parser_settings resp_settings;
  http_parser_settings_init(&resp_settings);
  resp_settings.on_chunk_header = on_chunk_header;
  resp_settings.on_chunk_complete = on_chunk_complete;

  http_parser *resp_parser = (http_parser*) malloc (sizeof(http_parser));
  http_parser_init(resp_parser, HTTP_RESPONSE);

  recv_chunk* chunk_flag = (recv_chunk*) malloc(sizeof(chunk_flag));
  chunk_flag->chunk_flag = 0;
  resp_parser->data = chunk_flag;

  //RESPONSE PARSER INITIALIZED

  int previous_connection = -1;

  int i = 0;
  char recv_h[8196], send_r[8196];
  int num;

  char* result_host = NULL;
  int result_host_len;
  char* result_url = NULL;
  int result_url_len;

  char* old_host = NULL;
  char* old_url = NULL;

  while (1) {
    chunk_flag->chunk_flag = 0;
    printf("Wait for request....\n");
    if ((num = recv(home_connection, recv_h, 8196, 0)) == 0) {
      printf("Client closed connection\n");
      free(req_parser);
      free(resolve_data);
      free(resp_parser);
      free(chunk_flag);
      close(home_connection);
      if (previous_connection != -1)
        close(previous_connection);
      return NULL;
    }

    //RECIEVED REQUEST FROM CLIENT

    if (result_host && result_url) {
      free(old_url);
      free(old_host);
      old_url = (char*) malloc((result_url_len + 1) * sizeof(char));
      old_host = (char*) malloc((result_host_len + 1) * sizeof(char));
      strncpy(old_url, result_url, result_url_len);
      strncpy(old_host, result_host, result_host_len);
    }

    i = http_parser_execute(req_parser, &req_settings, recv_h, num);
    result_host = ((host_resolve*)req_parser->data)->host_name;
    result_host_len = ((host_resolve*)req_parser->data)->host_len;
    result_url = ((host_resolve*)req_parser->data)->url;
    result_url_len = ((host_resolve*)req_parser->data)->url_len;

    if (result_host == NULL) {
      if (result_url == NULL) {
        printf("Parser error: %s\n", http_errno_description(HTTP_PARSER_ERRNO(req_parser)));
        free(req_parser);
        free(resolve_data);
        free(resp_parser);
        free(chunk_flag);
        close(home_connection);
        if (previous_connection != -1)
          close(previous_connection);
        return (void*)-1;
      }
      else {
        result_host = result_url;
        result_host_len = result_url_len;
      }
    }

    if (previous_connection != -1 && strncmp(result_host, old_host, result_host_len) != 0) {
      printf("Request to another Host, closing previous_connection....");
      close(previous_connection);
      previous_connection = -1;
    }

    //CHECK IF IN CACHE

    cache_list_el* cache_p = find_by_url_and_host(result_url, result_url_len, result_host, result_host_len);
    //IMPORTANT: IF find_by_url_and_host FAILED, END OF THE LIST IS BLOCKED, UNTIL NEW NODE WILL BE ADDED (TO PREVENT MULTIPLE INSERTS IN LIST)
    if (cache_p) {
      printf("Hit cache!\n");
    }
    else {
      printf("No cache node, adding...\n");
      cache_p = cache_add(result_url, result_url_len, result_host, result_host_len);
    }
    //IF NOT IN CACHE, ADD NEW CACHE NODE

    int transmit_result = 0;
    data_subentry* data = get_data_head(cache_p);
    if (!data) {
      printf("No data in cache or corrupted\n");
      if (previous_connection == -1) {
        //IF NO previous_connection, OPEN NEW
        previous_connection = connect_to_server(result_host, result_host_len);
        if (previous_connection == -1) {
          abort_set(cache_p);
          free(req_parser);
          free(resolve_data);
          free(resp_parser);
          free(chunk_flag);
          if (previous_connection != -1)
            close(previous_connection);
          close(home_connection);
          return (void*)-1;  
        }
      }
      transmit_result = transmit_data_from_server(&previous_connection, home_connection, resp_parser, &resp_settings, recv_h, cache_p);
    }
    else {
      transmit_result = send_cached_data(home_connection, data);
    }
    if (transmit_result == 1) {
      free(resolve_data);
      free(resp_parser);
      free(chunk_flag);
      close(home_connection);
      if (previous_connection != -1)
        close(previous_connection);
      return NULL;
    }
  }
}

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  int home_port = atoi(argv[1]);

  struct sigaction act;

  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, NULL);

  int socket_fd;
  if ((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
    perror("Can't create listening socket");
    return 2;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(home_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(socket_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
    perror("Can't bind socket");
    return 4;
  }

  if (listen(socket_fd, 510) != 0) {
    perror("Socket can't listen");
    return 3;
  }

  pthread_attr_t worker_attr;
  pthread_attr_init(&worker_attr);
  pthread_attr_setdetachstate(&worker_attr, PTHREAD_CREATE_DETACHED);

  while (1) {
    long home_connection;
    if ((home_connection = accept(socket_fd, NULL, NULL)) == -1) {
      perror("Can't accept home connection");
      return 5;
    }
    printf("Creating new home_connection....\n");

    pthread_t thread;
    if (pthread_create(&thread, &worker_attr, serve_client, (void*)home_connection) != 0) {
      perror("Error creating new thread");
      close(home_connection);
    }
  }
  close(socket_fd);
  return 0;
}