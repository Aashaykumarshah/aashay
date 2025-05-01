#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

/* Sender side structures */
struct pkt sender_buffer[SEQSPACE];
bool sender_ack_received[SEQSPACE];
int base;
int nextseqnum;
bool timer_running;

/* Receiver side structures */
struct pkt receiver_buffer[SEQSPACE];
bool receiver_buffer_filled[SEQSPACE];
int expectedseqnum;

/* Checksum computation */
int compute_checksum(struct pkt packet) {
    int checksum = 0;
    int i;
    checksum += packet.seqnum;
    checksum += packet.acknum;
    for (i = 0; i < 20; i++) {
        checksum += (unsigned char)packet.payload[i];
    }
    return checksum;
}

/* Timer control */
void start_timer() {
    if (!timer_running) {
        starttimer(A, RTT);
        timer_running = true;
    }
}

void stop_timer() {
    stoptimer(A);
    timer_running = false;
}

bool in_window(int seqnum) {
    return ((seqnum >= base && seqnum < base + WINDOWSIZE) ||
            (base + WINDOWSIZE >= SEQSPACE && seqnum < (base + WINDOWSIZE) % SEQSPACE));
}

/* A_output: send data to B */
void A_output(struct msg message) {
    if (in_window(nextseqnum)) {
        struct pkt packet;
        packet.seqnum = nextseqnum;
        packet.acknum = NOTINUSE;
        memcpy(packet.payload, message.data, 20);
        packet.checksum = compute_checksum(packet);

        sender_buffer[nextseqnum] = packet;
        sender_ack_received[nextseqnum] = false;

        tolayer3(A, packet);
        if (base == nextseqnum) {
            start_timer();
        }

        nextseqnum = (nextseqnum + 1) % SEQSPACE;
    }
}

/* A_input: process ACK from B */
void A_input(struct pkt packet) {
    int checksum = compute_checksum(packet);
    if (checksum == packet.checksum && in_window(packet.acknum)) {
        sender_ack_received[packet.acknum] = true;

        while (sender_ack_received[base]) {
            sender_ack_received[base] = false;
            base = (base + 1) % SEQSPACE;
        }

        if (base == nextseqnum) {
            stop_timer();
        } else {
            start_timer();
        }
    }
}

/* A_timerinterrupt: retransmit base packet */
void A_timerinterrupt(void) {
    if (!sender_ack_received[base]) {
        tolayer3(A, sender_buffer[base]);
    }
    start_timer();
}

/* A_init: initialize sender */
void A_init(void) {
    int i;
    base = 0;
    nextseqnum = 0;
    timer_running = false;
    for (i = 0; i < SEQSPACE; i++) {
        sender_ack_received[i] = false;
    }
}

/* B_input: receive and acknowledge */
void B_input(struct pkt packet) {
    int checksum = compute_checksum(packet);
    struct pkt ack_pkt;

    if (checksum == packet.checksum) {
        if (!receiver_buffer_filled[packet.seqnum]) {
            receiver_buffer[packet.seqnum] = packet;
            receiver_buffer_filled[packet.seqnum] = true;
        }

        ack_pkt.seqnum = NOTINUSE;
        ack_pkt.acknum = packet.seqnum;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = compute_checksum(ack_pkt);
        tolayer3(B, ack_pkt);

        while (receiver_buffer_filled[expectedseqnum]) {
            tolayer5(B, receiver_buffer[expectedseqnum].payload);
            receiver_buffer_filled[expectedseqnum] = false;
            expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        }
    }
}

/* B_init: initialize receiver */
void B_init(void) {
    int i;
    expectedseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        receiver_buffer_filled[i] = false;
    }
}

/* Unused */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}

