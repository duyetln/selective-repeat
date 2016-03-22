#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXSEQNUM 60000
#define MAXPACKETSIZE 1000
#define WINDSIZE 2500
#define DATASIZE 500
#define TIMEOUT 500 // milliseconds
#define DEBUG 1

typedef struct ack_packet *ack_packet_t;
typedef struct data_packet *data_packet_t;
typedef struct timeval *timeval_t;

struct ack_packet {
  unsigned int seq_num;
  unsigned int acc_seq_num;
};

struct data_packet {
  unsigned int seq_num;
  unsigned int length;
  unsigned char first;
  unsigned char end;
  char *data;
  ack_packet_t ack;
  data_packet_t next;
  timeval_t sent_at;
};

#define SEQ_NUM(p) ((p[0]<<8)+p[1])
#define ACC_SEQ_NUM(p) ((p[2]<<8)+p[3])

void
error(char *msg) {
  perror(msg);
  exit(1);
}

data_packet_t
create_data_packet(FILE *file, unsigned int *seq_num) {
  data_packet_t p;
  char buff[DATASIZE];
  unsigned long offset;

  p = (data_packet_t)calloc(1, sizeof(struct data_packet));
  p->first = ftell(file) == 0 ? 1 : 0;
  p->length = fread(buff, sizeof(char), DATASIZE, file);
  p->seq_num = *seq_num;
  if (p->length > 0) {
    p->data = (char *)calloc(p->length, sizeof(char));
    memcpy(p->data, buff, p->length);
  }

  offset = ftell(file);
  if (fgetc(file) == EOF)
    p->end = 1;
  else {
    fseek(file, offset, SEEK_SET);
    p->end = 0;
  }

  *seq_num = *seq_num + p->length;
  return p;
}

void
destroy_data_packet(data_packet_t p) {
  if (p) {
    if (p->length > 0)
      free(p->data);
    if (p->ack)
      free(p->ack);
    if (p->sent_at)
      free(p->sent_at);
    free(p);
  }
}

void
send_data(data_packet_t p, int sckt, struct sockaddr_in *recv_addr, socklen_t recv_addr_len) {
  unsigned int len = 4 + p->length;
  unsigned int lenfe = ((p->length << 4) + (p->first << 1) + (p->end));
  unsigned char data[len];
  data[0] = (p->seq_num >> 8) & 255;
  data[1] =  p->seq_num & 255;
  data[2] = (lenfe >> 8) & 255;
  data[3] =  lenfe & 255;

  if (p->length > 0)
    memcpy(&data[4], p->data, p->length);
  if (!p->sent_at)
    p->sent_at = (timeval_t)calloc(1, sizeof(struct timeval));
  gettimeofday(p->sent_at, NULL);
  sendto(sckt, data, len, 0, (struct sockaddr *)recv_addr, recv_addr_len);
  if (DEBUG)
    printf("Sending %d %d %d %d\n", p->seq_num, p->length, p->first, p->end);
}

void
enqueue_data_packet(data_packet_t *head, data_packet_t *tail, data_packet_t p) {
  if (*head == NULL || *tail == NULL) {
    *head = p;
    *tail = p;
  } else {
    (*tail)->next = p;
    (*tail) = p;
  }
}

data_packet_t
dequeue_data_packet(data_packet_t *head) {
  if (*head == NULL)
    return NULL;
  else {
    data_packet_t p = *head;
    *head = (*head)->next;
    return p;
  }
}

void
destroy_queue(data_packet_t head) {
  data_packet_t p;
  while((p = dequeue_data_packet(&head)))
    destroy_data_packet(p);
}

void
ack_data_packet(data_packet_t head, ack_packet_t ap) {
  while(head != NULL) {
    if (head->seq_num == ap->seq_num) {
      head->ack = ap;
      break;
    }
    head = head->next;
  }
}

unsigned int
compute_acc_seq_num(data_packet_t head) {
  data_packet_t acc = NULL;
  while(head && head->ack) {
    acc = head;
    head = head->next;
  }
  return acc != NULL ? acc->seq_num + acc->length : 0;
}

unsigned int
unacked_packet_num(data_packet_t head) {
  unsigned int cnt = 0;
  while(head != NULL) {
    if (!head->ack)
      cnt++;
    head = head->next;
  }
  return cnt;
}

int
main(int argc, char **argv) {
  if (argc < 6)
    error("Usage: ./sender port recvhost recvport dropprob filename");

  struct timeval timeout;
  struct sockaddr_in recv_addr;
  struct sockaddr_in send_addr;
  socklen_t recv_addr_len = sizeof(recv_addr);
  socklen_t send_addr_len = sizeof(send_addr);

  int sckt;
  unsigned int packet_len;
  unsigned char packet[MAXPACKETSIZE];

  unsigned int port = atoi(argv[1]);
  unsigned int drop = atof(argv[4]) * 100;
  unsigned int recv_port = atoi(argv[3]);
  char *recv_host = argv[2];
  char *filename = argv[5];

  FILE *file;
  data_packet_t head = NULL;
  data_packet_t tail = NULL;
  unsigned int seq_num = 0;
  unsigned int acc_seq_num = 0;

  data_packet_t dp;
  ack_packet_t ap;
  unsigned char end;

  if ((sckt = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    error("Cannot open sender socket");

  send_addr.sin_family = AF_INET;
  send_addr.sin_addr.s_addr = INADDR_ANY;
  send_addr.sin_port = htons(port);

  if (bind(sckt, (struct sockaddr *)&send_addr, send_addr_len) < 0)
    error("Cannot bind sender socket");

  timeout.tv_sec = 0;
  timeout.tv_usec = TIMEOUT * 1000;
  if (setsockopt(sckt, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&timeout, sizeof(struct timeval)) < 0)
    error("Cannot set sender socket options");

  recv_addr.sin_family = AF_INET;
  recv_addr.sin_port = htons(recv_port);

  if (inet_aton(recv_host, &recv_addr.sin_addr) == 0)
    error("Invalid receiver host");

  if ((file = fopen(filename, "r")) == NULL)
    error("Cannot open file");

  dp = create_data_packet(file, &seq_num);
  send_data(dp, sckt, &recv_addr, recv_addr_len);
  enqueue_data_packet(&head, &tail, dp);
  end = dp->end;

  for(;;) {
    if (DEBUG)
      printf("\n");

    if (acc_seq_num == seq_num && seq_num >= MAXSEQNUM) {
      destroy_queue(head);
      head = NULL;
      tail = NULL;
      seq_num = 0;
      acc_seq_num = 0;
    }

    unsigned int left_side = acc_seq_num;
    unsigned int right_side = left_side + WINDSIZE;

    while(!end && left_side <= seq_num && seq_num < right_side && seq_num < MAXSEQNUM) {
      dp = create_data_packet(file, &seq_num);
      send_data(dp, sckt, &recv_addr, recv_addr_len);
      enqueue_data_packet(&head, &tail, dp);
      end = dp->end;
    }

    do {
      unsigned int unack_num = unacked_packet_num(head);
      while(unack_num-- > 0) {
        memset(packet, 0, MAXPACKETSIZE);
        packet_len = recvfrom(sckt, packet, MAXPACKETSIZE, 0, (struct sockaddr *)&recv_addr, &recv_addr_len);
        if (packet_len <= 0)
          continue;

        if (rand() % 101 <= drop && drop > 0) {
          if (DEBUG)
            printf("Packet loss %d\n", SEQ_NUM(packet));
          continue;
        }

        ap = (ack_packet_t)calloc(1, sizeof(struct ack_packet));
        ap->seq_num = SEQ_NUM(packet);
        ap->acc_seq_num = ACC_SEQ_NUM(packet);

        if (DEBUG)
          printf("Received %d %d\n", ap->seq_num, ap->acc_seq_num);

        ack_data_packet(head, ap);
      }

      acc_seq_num = compute_acc_seq_num(head);
      if (acc_seq_num < seq_num) {
        data_packet_t p = head;
        struct timeval now;
        while (p != NULL) {
          if (!p->ack) {
            gettimeofday(&now, NULL);
            if ((now.tv_sec - p->sent_at->tv_sec) * 1000000L + (now.tv_usec - p->sent_at->tv_usec) > TIMEOUT * 1000L)
              send_data(p, sckt, &recv_addr, recv_addr_len);
          }
          p = p->next;
        }
      }
    } while (acc_seq_num < seq_num && seq_num >= MAXSEQNUM);

    if (acc_seq_num == seq_num && dp->end) {
      printf("File transfer complete\n");
      break; // complete
    }
  }

  destroy_queue(head);
  fclose(file);
  return 0;
}
