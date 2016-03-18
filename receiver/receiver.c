#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXPACKETSIZE 1000
#define WINDSIZE 2500
#define DEBUG 1

typedef struct data_packet *data_packet_t;

struct data_packet {
  unsigned int seq_num;
  unsigned int length;
  unsigned char first;
  unsigned char end;
  unsigned char dup;
  char *data;
  data_packet_t next;
};

void
error(char *msg) {
  perror(msg);
  exit(1);
}

#define SEQ_NUM(p) ((p[0]<<8)|p[1])
#define LENGTH(p) (((p[2]<<8)|p[3])>>4)
#define FIRST(p) ((p[3]>>1)&1)
#define END(p) (p[3]&1)

char *
DATA(char *packet) {
  unsigned int len = LENGTH(packet);
  if (len > 0) {
    char *data = (char *)calloc(1, len);
    memcpy(data, (char *)&packet[4], len);
    return data;
  } else
    return NULL;
}

unsigned int
compute_acc_seq_num(data_packet_t head) {
  data_packet_t acc = NULL;
  if (head->seq_num == 0)
    acc = head;

  if (acc != NULL) {
    while (acc->next != NULL && acc->seq_num + acc->length == acc->next->seq_num)
      acc = acc->next;
  }

  return acc != NULL ? acc->seq_num + acc->length : 0;
}

void
enqueue_data_packet(data_packet_t *head, data_packet_t p) {
  if (*head == NULL)
    *head = p;
  else {
    data_packet_t curr = *head;
    if (curr->seq_num > p->seq_num) {
      p->next = curr;
      *head = p;
    } else if (curr->seq_num < p->seq_num) {
      while (curr->next != NULL && curr->next->seq_num < p->seq_num)
        curr = curr->next;

      if (curr->next == NULL)
        curr->next = p;
      else if (curr->next->seq_num > p->seq_num) {
        p->next = curr->next;
        curr->next = p;
      } else
        p->dup = 1;
    } else
      p->dup = 1;
  }
}

void
send_ack(unsigned int seq_num, unsigned int acc_seq_num, int sckt, struct sockaddr_in *send_addr, socklen_t send_addr_len) {
  unsigned int len = 4;
  char ack[len];
  ack[0] = (seq_num >> 8) & 255;
  ack[1] =  seq_num & 255;
  ack[2] = (acc_seq_num >> 8) & 255;
  ack[3] =  acc_seq_num & 255;
  sendto(sckt, ack, len, 0, (struct sockaddr *)send_addr, send_addr_len);
  if (DEBUG)
    printf("Sending %d %d\n", seq_num, acc_seq_num);
}

void
write_to_file(data_packet_t head, char *filename) {
  int fd;
  if ((fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR)) == -1)
    error("Cannot open file");

  while(head != NULL) {
    data_packet_t p = head;
    head = head->next;

    if (p->length > 0 && p->data) {
      write(fd, p->data, p->length);
      free(p->data);
    }

    free(p);
  }

  close(fd);
}

int
main(int argc, char **argv) {
  if (argc < 4)
    error("Usage: ./receiver port dropprob filename");

  struct sockaddr_in recv_addr;
  struct sockaddr_in send_addr;
  socklen_t recv_addr_len = sizeof(recv_addr);
  socklen_t send_addr_len = sizeof(send_addr);

  int sckt;
  unsigned int packet_len;
  char packet[MAXPACKETSIZE];

  data_packet_t queue = NULL;
  unsigned char complete = 0;
  unsigned int acc_seq_num = 0;
  unsigned int end_seq_num = -1;

  unsigned int port = atoi(argv[1]);
  unsigned int drop = atof(argv[2]) * 100;
  char *filename = argv[3];

  if ((sckt = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    error("Cannot open receiver socket");

  recv_addr.sin_family = AF_INET;
  recv_addr.sin_addr.s_addr = INADDR_ANY;
  recv_addr.sin_port = htons(port);

  if (bind(sckt, (struct sockaddr *)&recv_addr, recv_addr_len) < 0)
    error("Cannot bind receiver socket");

  for(;;) {
    if (DEBUG)
      printf("\nListening\n");

    memset(packet, 0, MAXPACKETSIZE);
    packet_len = recvfrom(sckt, packet, MAXPACKETSIZE, 0, (struct sockaddr *)&send_addr, &send_addr_len);

    if (rand() % 101 <= drop && drop > 0) {
      if (DEBUG)
        printf("Packet loss %d\n", SEQ_NUM(packet));
      continue;
    }

    unsigned int seq_num = SEQ_NUM(packet);
    unsigned int length = LENGTH(packet);
    unsigned char first = FIRST(packet);
    unsigned char end = END(packet);
    char *data = DATA(packet);

    unsigned int left_side = acc_seq_num;
    unsigned int right_side = acc_seq_num + WINDSIZE;

    if (DEBUG)
      printf("Received %d %d %d %d %d\n", seq_num, length, first, end, acc_seq_num);

    if (seq_num >= right_side)
      continue;
    else if (seq_num < left_side && seq_num > 0) {
      send_ack(seq_num, acc_seq_num, sckt, &send_addr, send_addr_len);
      continue;
    }

    // seq num has been reset
    if (seq_num == 0 && !first) {
      write_to_file(queue, filename);
      acc_seq_num = 0;
      queue = NULL;
    }

    data_packet_t p = (data_packet_t)calloc(1, sizeof(struct data_packet));
    p->seq_num = seq_num;
    p->length = length;
    p->first = first;
    p->end = end;
    p->data = data;

    enqueue_data_packet(&queue, p);
    acc_seq_num = compute_acc_seq_num(queue);
    send_ack(seq_num, acc_seq_num, sckt, &send_addr, send_addr_len);

    if (p->dup) {
      free(p->data);
      free(p);
    }

    if (p->end)
      end_seq_num = p->seq_num + p->length;

    if (acc_seq_num == end_seq_num && !complete) {
      write_to_file(queue, filename);
      complete = 1;
      if (DEBUG)
        printf("File transfer complete\n");
    }
  }

  return 0;
}
