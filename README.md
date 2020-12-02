# CAB403: Systems Programming ~ Distributed Systems assignment
Systems Programming Distributed Assignment

This application uses two networked programs, one a client and one a server, using the C programming language and making use of technologies such as sockets, threads and process management system calls.

The first of these programs (the client) is the controller, a command-line utility that performs no useful work by itself, but is responsible for allowing the user to send commands and receive output from the second program.

The second of these programs (the server) is the overseer. The overseer's main job is to launch other programs at the behest of the controller, and monitor them. The controller can then be used to request information about the processes, ask that they be terminated and so forth.

Usage:

`overseer <port>`

`controller <address> <port> {[-o out_file] [-log log_file] [-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}`
