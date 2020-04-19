# Ping Application Description

This is a ping CLI program written in C and built using Xcode 10.3, tested successfully on macOS Mojave. The CLI app sends an ICMP ECHO packet in a loop to the server of your choice and listens for an ICMP REPLY packet. 

## Usage Instructions

1. To Compile the C file : ```gcc -o myPingApp ping_CLI.c```
2. To run the ping application: ```sudo ./myPingApp  www.example.com or X.X.X.X```
3. ```CTRL + C ``` to stop running the infinite loop

## Features

* Report lost packages for each message
* Report RTT (Round Trip Times) for each message
* Supports both IPv4 and IPv6



