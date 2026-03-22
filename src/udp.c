#include <errno.h>
#include <stdint.h>
#include <strings.h>
#include <stdio.h>

#include <pthread.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#include <sys/select.h>
#endif


#include "udp.h"

// Simple hash function for UDP address/port to array index
static int get_connection_index(struct sockaddr_in *addr, int array_size) {
    uint32_t ip = addr->sin_addr.s_addr;
    uint16_t port = addr->sin_port;
    
    // Combine IP and port with XOR and multiply
    uint32_t hash = ip ^ (port << 16) ^ port;
    
    // Simple mixing
    hash = hash * 2654435761u;  // Knuth's multiplicative hash
    
    return hash % array_size;
}

static int udp_port = 0;
static int udp_running = 1;

static pthread_t udp_thread_handle;
static pthread_attr_t udp_attr;

static struct sockaddr_in serve;

static int udp_open(int port) {
#ifdef _WIN32
  static int first = 1;
  if (first) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    first = 0;
  }
#endif
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(int));
    memset(&serve, 0, sizeof(serve));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    bzero(&serve, sizeof(serve));
#endif
    serve.sin_family = AF_INET;
    serve.sin_addr.s_addr = htonl(INADDR_ANY);
    serve.sin_port = htons(port);
    if (bind(sock, (struct sockaddr *)&serve, sizeof(serve)) >= 0) {
        return sock;
    }
    return -1;
}

typedef struct ctx_s {
  uint32_t id;
  int (*fn)(struct ctx_s *w, char *, size_t);
  int udp;
  int me;
  uint32_t ip;
  uint16_t port;
  char first;
#if 0
  size_t log_len;
  char *log;
#endif
} ctx_t;

uint32_t ctx_ctr = 0;
void ctx_init(ctx_t *w, int (*fn)(ctx_t *w, char *, size_t)) {
  w->id = ctx_ctr++;
  w->fn = fn;
  w->udp = 0;
  w->me = 0;
  w->ip = 0;
  w->port = 0;
  w->first = 1;
}

static int (*udp_process)(char *, size_t) = NULL;

int ctx_consume(ctx_t *w, char *s, size_t len) {
  //printf("(%d)consume{%s}\n", w->id, s);
  if (w->first && w->udp) {
    //printf("me:%d ip:%x port:%d\n", w->me, w->ip, w->port);
    w->first = 0;
  }
  if (udp_process) udp_process(s, len);
}

typedef struct {
  ctx_t w;
  int in_use;
  int last_use;
} udp_state_t;

#define UDP_PORT_MAX (127)

int ctx_consume(ctx_t *w, char *s, size_t len);

static void *udp_main(void *arg) {
  if (udp_port <= 0) {
    return NULL;
  }
  int sock = udp_open(udp_port);
  if (sock < 0) {
    puts("# udp thread cannot run");
    return NULL;
  }
  //util_set_thread_name("udp");
  struct sockaddr_in client;
#ifdef _WIN32
  int client_len = sizeof(client);
#else
  unsigned int client_len = sizeof(client);
#endif
  char line[1024];
  fd_set readfds;
  struct timeval timeout;
  udp_state_t user[UDP_PORT_MAX];
  for (int i = 0; i < UDP_PORT_MAX; i++) {
    ctx_init(&user[i].w, ctx_consume);
    user[i].in_use = 0;
    user[i].last_use = 0;
  }
  while (udp_running) {
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int ready = select(sock+1, &readfds, NULL, NULL, &timeout);
    if (ready > 0 && FD_ISSET(sock, &readfds)) {
      ssize_t n = recvfrom(sock, line, sizeof(line), 0, (struct sockaddr *)&client, &client_len);
      if (n > 0) {
        line[n] = '\0';
        int me = get_connection_index(&client, UDP_PORT_MAX);
        ctx_t *w = &user[me].w;
        if (w->udp == 0) {
          struct sockaddr_in *addr = &client;
          w->udp = 1;
          w->me = me;
          w->ip = addr->sin_addr.s_addr;
          w->port = addr->sin_port;
        }
        for (int i=0; i<n; i++) {
          if (line[i] == '\n' || line[i] == '\r') {
              line[i] = '\0';
              n = i;
              break;
          }
        }
        for (int i=n-1; i>=0; i--) {
          if (line[i] == ' ' || line[i] == '\t') {
            line[i] = '\0';
            n--;
          } else break;
        }
        //printf("(udp/%d)\n", me);
        if (w && w->fn && n>0) w->fn(w, line, n);
#if 0
        if (w->log_len) {
          sendto(sock, w->log, w->log_len, 0, (struct sockaddr *)&client, client_len);
        }
#endif
        //
      } else {
        if (errno == EAGAIN) continue;
      }
    } else if (ready == 0) {
      // timeout
    } else {
      perror("# select");
    }
  }
  for (int i = 0; i < UDP_PORT_MAX; i++) {
    // need to free alloc-ed stuff here
  }
  return NULL;
}

int udp_start(int port, int (*fn)(char *, size_t n)) {
  if (port == 0) return 0;
  udp_process = fn;
  udp_port = port;
  udp_running = 1;
  pthread_attr_init(&udp_attr);
  pthread_attr_setstacksize(&udp_attr, 2 * 1024 * 1024);
  pthread_create(&udp_thread_handle, &udp_attr, udp_main, NULL);
  pthread_detach(udp_thread_handle);
  return port;
}

void udp_stop(void) {
  udp_running = 0;
#ifdef _WIN32
  WSACleanup();
#endif
}

int udp_info(void) {
  return udp_port;
}
