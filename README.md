# NetMessenger - IE3010 Network Programming Assignment

## Student Details

Registration Number: IT23756632

Module: IE3010 - Network Programming

Project Title: NetMessenger - A Multi-Client Chat and File-Sharing Platform over TCP/IP

GitHub Repository:
https://github.com/SithuminiGamage13/NetMessenger_IT23756632


## Personalised Configuration

Registration Number: IT23756632

Numeric Part:
23756632

Last Four Digits:
6632

Server Port:
12632

Port Calculation:
6000 + 6632 = 12632

Node ID:
NID:7566

Server Source File:
server_6632.c

Client Source File:
client_6632.c

Makefile:
Makefile_6632

Log File:
netmsg_IT23756632.log

Server Storage Path:
./storage/IT23756632/<sender_username>/<filename>

Final Submission ZIP:
IE3010_IT23756632.zip


## Project Description

NetMessenger is a TCP/IP based multi-client chat and file-sharing application implemented in C using the BSD sockets API.

The application consists of one server and multiple clients. The server supports multiple simultaneous clients using POSIX threads.

The system implements the communication protocol specified in the IE3010 assignment and includes personalised values based on registration number IT23756632.


## Implemented Features

- TCP client/server communication
- Multi-client concurrency using pthreads
- Unique username registration
- Connected-user listing using LIST
- Duplicate username detection
- Broadcast messaging using BCAST
- Private messaging using PMSG
- Chat room creation and joining using JOIN
- Leaving rooms using LEAVE
- Room listing using ROOMS
- Room messaging using RMSG
- User-to-user file transfer using SENDFILE
- Room-targeted file transfer support
- Exact file byte handling
- Personalised server-side file storage
- File forwarding to receiving clients
- Graceful disconnect handling
- Unexpected disconnect cleanup
- Invalid command handling
- Invalid user handling
- Invalid room handling
- TCP line framing
- Server-side logging
- Join and leave presence notifications
- Rate limiting / flood protection


## Optional Extension

A rate-limiting mechanism was implemented as an optional extension.

Each client is allowed a maximum of:

8 commands within 5 seconds

If the limit is exceeded, the server returns:

ERR 010 RATE_LIMITED NID:7566


## Build Instructions

Open a terminal inside the project directory and run:

```bash
make -f Makefile_6632 clean
make -f Makefile_6632
