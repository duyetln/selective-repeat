# Selective Repeat

## Overview

A simple prototype of reliable data transfer using Selective Repeat Protocol.

## Usage

First, run the receiver program:
```
receiver PORT LOSSPROB OUTPUTFILENAME
```
Second, run the sender program:
```
sender PORT RECVHOST RECVPORT LOSSPROB FILENAME
```
`LOSSPROB` is the simulated probability that a packet (either data or ack) is dropped.

## MECHANICS

The sender side sends `DATA` packets and the receiver side sends `ACK` packets. The format of both packet types are outlined below.
Each `DATA` packet transfers at maximum 500 bytes and uses sequence number for in-order data transfer. Sequence number starts at 0 and resets when it reaches 60000.
The sender side will resend a `DATA` packet if it is not acknowledged by the receiver after 500msec.

```
DATA
 0       1        2       3            4
 0123456701234567 012345670123 4 5 6 7 01234567
+----------------+------------+-+-+-+-+--------
|SEQ NUM         |LENGTH      |/|/|F|E|DATA...
+----------------+------------+-+-+-+-+--------
                                   | |
                                   | +-> last packet bit
                                   +---> first packet bit

ACK
 0       1        2       3
 0123456701234567 0123456701234567
+----------------+----------------+
|SEQ NUM         |ACC SEQ NUM     |
+----------------+----------------+
                  |
                  +-> left side of receiving window
```
