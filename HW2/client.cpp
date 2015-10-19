#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <iostream>
#include <assert.h>

using namespace std;

#define PORTNUM 50000

// Helper method to simulate time delay
void Delay(double delayInSeconds)
{
    clock_t before = clock();
    while (true)
    {
        clock_t now = clock();
        double time = (now - before) / (double)CLOCKS_PER_SEC;

        // Break out of loop, if approximately one ms has elapsed;
        if (time >= delayInSeconds)
        {
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int s;
    struct sockaddr_in server;
    int server_addr = INADDR_ANY;

    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [optional: server IP]\n", argv[0]);
        exit(1);
    }

    if (argc == 2)
    {
        server_addr = inet_addr(argv[1]);
    }

    string family, person;
    int arrival, departure;
    int last_arrival = 0;
    cin >> family >> person >> arrival >> departure;

    while (cin)
    {
        int arrival_diff = arrival - last_arrival;
        // Apply delay between arrivals if necessary
        Delay(arrival_diff);

        // Fork a new process for each person arriving in the plaza
        int pid = fork();
        if (pid == 0)
        {
            s = socket(AF_INET, SOCK_DGRAM, 0); //create socket
            if (s == -1) {
                fprintf(stderr, "socket() Socket was not created: %s\n", strerror(errno));
                exit(1);
            }

            //server properties
            server.sin_family = AF_INET;
            server.sin_port = htons(PORTNUM);
            server.sin_addr.s_addr = INADDR_ANY; // server should be running on the same machine

            // Send arrival message
            string arrival_message = "A " + person + " " + family;
            if (sendto(s, arrival_message.c_str(), arrival_message.length(), 0, (struct sockaddr *)&server, sizeof(sockaddr_in)) < 0)
            {
                fprintf(stderr, "sendto() failed: %s\n", strerror(errno));
                exit(1);
            }

            // Wait for signal from the server that the person has entered the plaza
            char ack_message;
            int addr_len = sizeof(sockaddr_in);
            if (recvfrom(s, &ack_message, sizeof(char), 0, (struct sockaddr *)&server, &addr_len) < 0)
            {
                fprintf(stderr, "recvfrom() did not get a valid message: %s\n", strerror(errno));
                exit(1);
            }
            else
            {
                string message = "Unexpected ACK message from server: " + ack_message;
                assert(ack_message == 'E' && message.c_str());
            }

            // Apply delay simulating the time the person spent in the plaza
            Delay(departure);

            // Send a departure message to the server
            string departure_message = "D " + person + " " + family;
            if (sendto(s, departure_message.c_str(), departure_message.length(), 0, (struct sockaddr *)&server, sizeof(sockaddr_in)) < 0)
            {
                fprintf(stderr, "sendto() failed: %s\n", strerror(errno));
                exit(1);
            }

            if (close(s) < 0)
                fprintf(stderr, "close(s) failed.\n");

            return 0;
        }
        else if (pid > 0)
        {
            cin >> family >> person >> arrival >> departure;
        }
        else
        {
            fprintf(stderr, "Failed to fork new process: %s\n", strerror(errno));
            exit(1);
        }
    }

    s = socket(AF_INET, SOCK_DGRAM, 0); //create socket
    if (s == -1) {
        fprintf(stderr, "socket() Socket was not created: %s\n", strerror(errno));
        exit(1);
    }

    //server properties
    server.sin_family = AF_INET;
    server.sin_port = htons(PORTNUM);
    server.sin_addr.s_addr = INADDR_ANY;

    // Send exit message
    string exit_message = "X Dummy Dummy";
    if (sendto(s, exit_message.c_str(), exit_message.length(), 0, (struct sockaddr *)&server, sizeof(sockaddr_in)) < 0)
    {
        fprintf(stderr, "sendto() failed: %s\n", strerror(errno));
        exit(1);
    }

    return 0;
}


