#ifndef _UDP_H_
#define _UDP_H_

int udp_start(int port, int (*fn)(char *, size_t n));
void udp_stop(void);
int udp_info(void);

#endif
