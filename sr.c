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
bool sender_timer_running[SEQSPACE];
bool sender_ack_received[SEQSPACE];
int base;
int nextseqnum;

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

/* Timer utilities */
void start_packet_timer(int seqnum) {
    if (!sender_timer_running[seqnum]) {
        starttimer(A, RTT);
        sender_timer_running[seqnum] = true;
    }
}

void stop_packet_timer(int seqnum) {
    if (sender_timer_running[seqnum]) {
        stoptimer(A);
        sender_timer_running[seqnum] = false;
    }
}

bool is_seqnum_in_window(int seqnum) {
    if (base <= (base + WINDOWSIZE - 1) % SEQSPACE) {
        return (seqnum >= base) && (seqnum <= (base + WINDOWSIZE - 1) % SEQSPACE);
    } else {
        return (seqnum >= base) || (seqnum <= (base + WINDOWSIZE - 1) % SEQSPACE);
    }
}

/* A_output: called from layer5, passing the message to be sent to B */
void A_output(struct msg message) {
    if (is_seqnum_in_window(nextseqnum)) {
        struct pkt packet;
        packet.seqnum = nextseqnum;
        packet.acknum = NOTINUSE;
        memcpy(packet.payload, message.data, 20);
        packet.checksum = compute_checksum(packet);

        sender_buffer[nextseqnum] = packet;
        sender_ack_received[nextseqnum] = false;

        tolayer3(A, packet);
        start_packet_timer(nextseqnum);

        nextseqnum = (nextseqnum + 1) % SEQSPACE;
    }
}

/* A_input: called from layer3, when a packet arrives for A */
void A_input(struct pkt packet) {
    int checksum = compute_checksum(packet);
    if (checksum == packet.checksum && is_seqnum_in_window(packet.acknum)) {
        sender_ack_received[packet.acknum] = true;
        stop_packet_timer(packet.acknum);

        /* Slide the window if possible */
        while (sender_ack_received[base]) {
            sender_ack_received[base] = false;
            base = (base + 1) % SEQSPACE;
        }
    }
}

/* A_timerinterrupt: called when A's timer goes off */
void A_timerinterrupt(void) {
    int i;
    for (i = 0; i < SEQSPACE; i++) {
        if (sender_timer_running[i] && !sender_ack_received[i] && is_seqnum_in_window(i)) {
            tolayer3(A, sender_buffer[i]);
            start_packet_timer(i);
        }
    }
}

/* A_init: initialize A-side structures */
void A_init(void) {
    int i;
    base = 0;
    nextseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        sender_timer_running[i] = false;
        sender_ack_received[i] = false;
    }
}

/* B_input: called from layer3, when packet arrives for B */
void B_input(struct pkt packet) {
    int checksum;
    struct pkt ack_pkt;
    checksum = compute_checksum(packet);
    if (checksum == packet.checksum) {
        if (is_seqnum_in_window(packet.seqnum)) {
            if (!receiver_buffer_filled[packet.seqnum]) {
                receiver_buffer[packet.seqnum] = packet;
                receiver_buffer_filled[packet.seqnum] = true;
            }

            /* Send ACK */
            ack_pkt.seqnum = NOTINUSE;
            ack_pkt.acknum = packet.seqnum;
            memset(ack_pkt.payload, 0, 20);
            ack_pkt.checksum = compute_checksum(ack_pkt);
            tolayer3(B, ack_pkt);

            /* Deliver in-order packets */
            while (receiver_buffer_filled[expectedseqnum]) {
                tolayer5(B, receiver_buffer[expectedseqnum].payload);
                receiver_buffer_filled[expectedseqnum] = false;
                expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
            }
        }
    } else {
        /* Corrupted packet: send ACK for last correctly received packet */
        ack_pkt.seqnum = NOTINUSE;
        ack_pkt.acknum = (expectedseqnum + SEQSPACE - 1) % SEQSPACE;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = compute_checksum(ack_pkt);
        tolayer3(B, ack_pkt);
    }
}

/* B_init: initialize B-side structures */
void B_init(void) {
    int i;
    expectedseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        receiver_buffer_filled[i] = false;
    }
}

/* (optional) For completeness - unused */
void B_output(struct msg message) {}
void B_timerinterrupt(void) { }

