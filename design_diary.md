# NetMessenger Design Diary

## Student
IT23756632

## Project
NetMessenger - Multi-Client Chat and File-Sharing Platform over TCP/IP

## Initial Setup and Personalisation

I first calculated the personalised values from my registration number IT23756632.

The last four digits are 6632, so the server port was calculated as:

6000 + 6632 = 12632

The middle four digits of the numeric part are 7566, so the Node ID is:

NID:7566

The personalised files used in the project are:

- server_6632.c
- client_6632.c
- Makefile_6632
- netmsg_IT23756632.log

The personalised storage path is:

./storage/IT23756632/<sender_username>/<filename>

I started with a simple TCP server and client to verify socket creation, bind, listen, accept and connect.


## Registration and Persistent Connections

The REGISTER command was implemented first.

The initial version handled only one command and then disconnected, so the design was improved to keep the connection open and allow commands such as LIST and QUIT.

All OK and ERR responses include the personalised NID tag:

NID:7566


## Multi-Client Concurrency

The server was then changed to support multiple simultaneous clients using POSIX threads.

Each accepted client connection is handled by a separate thread.

A shared client table stores:

- socket descriptor
- registration state
- username
- rate-limit information

Mutexes were used to protect shared client and room data.

This allowed the server to support multiple users and detect duplicate usernames.


## Broadcast and Private Messaging

The BCAST command was implemented to send a message to all other connected clients.

The PMSG command was implemented to send a message to one named user.

Error handling was added for users that do not exist.


## Chat Rooms

Room support was implemented using a room structure containing:

- room name
- active state
- room membership information

JOIN creates a room if it does not already exist and adds the client to it.

LEAVE removes the client from a room.

ROOMS lists available rooms.

RMSG forwards a message only to users who are members of the selected room.


## File Transfer

The SENDFILE command required special handling because TCP is a byte-stream protocol.

The command contains:

SENDFILE <target> <filename> <filesize>

After the command line, the server reads exactly the number of raw bytes specified by the filesize.

Files are stored under:

./storage/IT23756632/<sender_username>/<filename>

The file is then forwarded to the target client or room members.

The receiving client stores files inside:

./received_files/

File integrity was verified using the Linux cmp command. The original file, server copy and receiving-client copy matched successfully.


## TCP Framing

Normal commands are line-based and terminated using a newline.

A recv_line function was used to read complete command lines.

A separate recv_exact function was used for file data so the server reads exactly the expected number of bytes.

This helps handle TCP fragmentation correctly.


## Disconnect Handling

Graceful disconnection is handled using the QUIT command.

Unexpected disconnections are also detected.

When a client disconnects, the server removes the client from the active user list and removes the client from any rooms.


## Server Logging

The server writes timestamped events to:

netmsg_IT23756632.log

The log records events such as:

- server startup
- registrations
- broadcasts
- file transfers
- graceful disconnections
- unexpected disconnections
- rate-limit events


## Optional Extension

Basic rate limiting was added as an optional extension.

Each client can send a maximum of 8 text commands within 5 seconds.

If the limit is exceeded, the server returns:

ERR 010 RATE_LIMITED NID:7566

After the rate-limit window resets, the client can continue sending commands normally.


## Main Challenges

The main challenges during development were:

- maintaining shared state safely between threads
- keeping responses consistent with the required protocol
- handling multiple connected clients
- reading exact file bytes over TCP
- dealing with malformed commands
- handling graceful and unexpected disconnects
- removing compiler warnings

The program was developed incrementally and tested after each major feature before moving to the next stage.
