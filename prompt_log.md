# AI Prompt Log

## Student
IT23756632

## Module
IE3010 - Network Programming

## Project
NetMessenger - Multi-Client Chat and File-Sharing Platform over TCP/IP


## Interaction 1 - Understanding the Assignment

Tool Used:
ChatGPT

Prompt / Request:
Asked for help understanding the assignment requirements and for a step-by-step plan to complete the NetMessenger project correctly.

How the Output Was Used:
The assignment was divided into smaller stages such as personalisation, TCP connection setup, registration, concurrency, messaging, rooms, file transfer, testing and documentation.

Evaluation / Changes:
The implementation plan was checked against the assignment specification before development started.


## Interaction 2 - Personalisation Values

Tool Used:
ChatGPT

Prompt / Request:
Provided registration number IT23756632 and asked to calculate the personalised values required by the assignment.

How the Output Was Used:
The following values were used:

- Port: 12632
- NID:7566
- server_6632.c
- client_6632.c
- Makefile_6632
- netmsg_IT23756632.log
- storage/IT23756632/
- IT23756632.zip


## Interaction 3 - Initial TCP Client and Server

Tool Used:
ChatGPT

Prompt / Request:
Asked for help creating the initial TCP server and client programs in C using BSD sockets.

How the Output Was Used:
The code was used as a starting point to establish a TCP connection between the client and server.

Evaluation / Changes:
The code was compiled using GCC. Syntax and compilation issues found during testing were corrected before continuing.


## Interaction 4 - REGISTER, LIST and QUIT

Tool Used:
ChatGPT

Prompt / Request:
Asked for help implementing REGISTER, LIST and QUIT according to the assignment protocol.

How the Output Was Used:
The server was updated to support user registration, connected-user listing and graceful disconnection.

Evaluation / Changes:
The initial implementation was improved so that the TCP connection remained open and several commands could be sent during the same client session.


## Interaction 5 - Multi-Client Concurrency

Tool Used:
ChatGPT

Prompt / Request:
Asked for help modifying the server to support several clients at the same time.

How the Output Was Used:
POSIX threads were introduced so each connected client could be handled by a separate thread.

Evaluation / Changes:
Mutexes were added to protect shared client and room data. Multiple clients were tested together and duplicate usernames were rejected.


## Interaction 6 - Broadcast and Private Messaging

Tool Used:
ChatGPT

Prompt / Request:
Asked for help implementing BCAST and PMSG.

How the Output Was Used:
Broadcast messages were forwarded to other connected clients and private messages were delivered only to the named target user.

Evaluation / Changes:
Unknown-user handling and invalid-format checks were tested and corrected where necessary.


## Interaction 7 - Chat Rooms

Tool Used:
ChatGPT

Prompt / Request:
Asked for help implementing JOIN, LEAVE, ROOMS and RMSG.

How the Output Was Used:
Room creation, membership tracking and room-based messaging were added to the server.

Evaluation / Changes:
Room creation, joining, listing, messaging, leaving and NOT_IN_ROOM behaviour were tested with multiple clients.


## Interaction 8 - File Transfer

Tool Used:
ChatGPT

Prompt / Request:
Asked for help implementing SENDFILE, exact-byte reception, personalised server-side storage and forwarding files to clients.

How the Output Was Used:
The server was updated to receive the exact number of raw bytes specified in the command, save the file under the personalised storage directory and forward it to the target.

Evaluation / Changes:
File integrity was verified using the Linux cmp command. The original file, server copy and receiving-client copy matched successfully.


## Interaction 9 - TCP Framing and Logging

Tool Used:
ChatGPT

Prompt / Request:
Asked for help improving TCP framing and adding server-side logging.

How the Output Was Used:
A line-based command reader and an exact-byte receive function were used. Timestamped logging was also added.

Evaluation / Changes:
The project was compiled using -Wall -Wextra and compiler warnings were reviewed and corrected before the final build.


## Interaction 10 - Error Handling and Disconnect Cleanup

Tool Used:
ChatGPT

Prompt / Request:
Asked for help handling invalid commands, duplicate usernames, missing users, room-related errors and unexpected disconnects.

How the Output Was Used:
Additional ERR responses and cleanup logic were added.

Evaluation / Changes:
Invalid commands were tested to confirm that the server returned the expected error without terminating the client session. Disconnect cleanup was also verified.


## Interaction 11 - Optional Rate Limiting

Tool Used:
ChatGPT

Prompt / Request:
Asked for a suitable optional extension that could improve the assignment.

How the Output Was Used:
A basic per-client rate-limiting mechanism was implemented.

Configuration:
8 commands within 5 seconds.

Rate Limit Response:
ERR 010 RATE_LIMITED NID:7566

Evaluation / Changes:
The extension was tested by sending LIST commands rapidly. The server returned the expected rate-limit error after the threshold was exceeded and normal processing resumed after the time window reset.


## Interaction 12 - Git and Incremental Development

Tool Used:
ChatGPT

Prompt / Request:
Asked for help using Git to commit and push the project incrementally.

How the Output Was Used:
The project was committed in stages, including registration, persistent sessions, concurrency, messaging, rooms, file transfer, logging and rate limiting.

Evaluation / Changes:
The Git history was checked to confirm that meaningful incremental commits were present rather than one final upload.


## Interaction 13 - Documentation and Report Preparation

Tool Used:
ChatGPT

Prompt / Request:
Asked for help preparing the README, design diary, prompt log, reflection and implementation report.

How the Output Was Used:
Draft content and report structure were created based on the implemented and tested features.

Evaluation / Changes:
The documentation was checked against the actual program behaviour and screenshots before final submission.


## Overall AI Use

AI was used as a development assistant for explanation, planning, debugging, code review, testing guidance and documentation support.

AI-generated output was not accepted automatically. The code was compiled, executed and tested in the CentOS environment, and changes were made when errors, warnings or incorrect behaviour were found.

The project was developed incrementally, and the final implementation was verified using multiple client terminals, file-integrity checks, server logs, Git history and runtime evidence.
