#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <queue>
#include <sstream>
#include <map>
#include <assert.h>
#include <iostream>
#include <cstring>

using namespace std;

#define PORTNUM             50000
#define MAX_MESSAGE_SIZE    1024

bool IsAMontague(string family);
bool IsACapulet(string family);
bool IsArrivalMessage(char message_type);
bool IsDepatureMessage(char message_type);
bool IsExitMessage(char message_type);
int HandleArrivalMessage(int socket, string person, string family, struct sockaddr * client_address);
int HandleDepatureMessage(int socket, string person, string family);

vector<string> waitingMontagues;
vector<string> waitingCapulets;
map<string, sockaddr_storage *> montague_client_addresses;
map<string, sockaddr_storage *> capulet_client_addresses;
unsigned int montaguesInPlaza = 0;
unsigned int capulatesInPlaza = 0;

const char ack_message = 'E'; // reply to clients that they have successfully entered the plaza

int main() {
    int s;
    socklen_t addrlen;
    struct sockaddr_in server_addr;
    bool shouldExit = false;
    memset(&server_addr, 0, sizeof(struct sockaddr_in)); // clear out address

    char message[MAX_MESSAGE_SIZE];

    //create socket
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) 
    {
        fprintf(stderr, "socket() Socket was not created: %s\n", strerror(errno));
        exit(2);
    }

    // ensure that the same IP address can be reused after for the socket in a single terminal instance
    const int optVal = 1;
    const socklen_t optLen = sizeof(optVal);
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&optVal, optLen) != 0) 
    {
        fprintf(stderr, "setsockopt() Error setting reuse-address option: %s\n", strerror(errno));
        exit(2);
    }

    //server socket properties
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORTNUM);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // bind() associate socket with port on the machine
    addrlen = sizeof(server_addr);
    if (bind(s, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) 
    {
        close(s);
        fprintf(stderr, "bind() Error binding server: %s\n", strerror(errno));
        exit(1);
    }

    // retrieve resolved server socket information
    if (getsockname(s, (struct sockaddr *) &server_addr, &addrlen) < 0) 
    {
        fprintf(stderr, "getsockname() failed to get port number: %s\n", strerror(errno));
        exit(1);
    }

    fprintf(stdout, "The assigned port is %d\n", ntohs(server_addr.sin_port));
    cout << endl;

    istringstream input_stream;
    char message_type;
    string person;
    string family;

    while (true)
    {
        sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(sockaddr_storage);
        
        memset(message, 0, MAX_MESSAGE_SIZE);
        // Wait for new message, and retrieve client information
        if (recvfrom(s, message, sizeof(message), 0, (struct sockaddr *)&client_addr, &addr_len) < 0)
        {
            fprintf(stderr, "recvfrom() did not get a valid message: %s\n", strerror(errno));
            exit(1);
        }

        // Parse message
        input_stream.clear();
        input_stream.str(message);
        input_stream >> message_type >> person >> family;

        if (IsArrivalMessage(message_type))
        {
            if (HandleArrivalMessage(s, person, family, (struct sockaddr *)&client_addr) < 0)
            {
                fprintf(stderr, "HandleArrivalMessage() failed: %s\n", strerror(errno));
                exit(1);
            }
        }
        else if (IsDepatureMessage(message_type))
        {
            if (HandleDepatureMessage(s, person, family) < 0)
            {
                fprintf(stderr, "HandleDepatureMessage() failed: %s\n", strerror(errno));
                exit(1);
            }

            // No more clients to process
            if (montaguesInPlaza == 0 && capulatesInPlaza == 0 && shouldExit)
            {
                break;
            }
        }
        else if (IsExitMessage(message_type))
        {
            shouldExit = true;
        }
        else
        {
            break;
        }
    }

    if (close(s) < 0)
        fprintf(stderr, "close(s) failed.\n");

    cout << endl << "Server finished" << endl;
    
    exit(0);
}

bool IsAMontague(string family)
{
    return strcmp(family.c_str(), "Montague") == 0;
}

bool IsACapulet(string family)
{
    return strcmp(family.c_str(), "Capulet") == 0;
}

bool IsArrivalMessage(char message_type)
{
    return toupper(message_type) == 'A';
}

bool IsDepatureMessage(char message_type)
{
    return toupper(message_type) == 'D';
}

bool IsExitMessage(char message_type)
{
    return toupper(message_type) == 'X';
}

int HandleArrivalMessage(int socket, string person, string family, struct sockaddr * _client_addr)
{
    if (IsAMontague(family))
    {
        cout << "Montague " << person << " arrives" << endl;
        if (capulatesInPlaza == 0)
        {
            montaguesInPlaza++;
            cout << "Montague " << person << " enters the plaza" << endl;
            // Notify client right away
            return sendto(socket, &ack_message, sizeof(char), 0, _client_addr, sizeof(sockaddr_storage));
        }
        else
        {
            // At least one capulete is in the plaza so the montague will have to wait until
            // the plaza is empty
            waitingMontagues.push_back(person);

            // snap-off a copy of the client's address on the heap to be used to notify the client
            // when it is able to enter the plaza
            sockaddr_storage * client_addr = new sockaddr_storage;
            memcpy(client_addr, _client_addr, sizeof(sockaddr_storage));
            montague_client_addresses.insert(make_pair(person, client_addr));
        }
    }
    else if (IsACapulet(family))
    {
        cout << "Capulet " << person << " arrives" << endl;
        if (montaguesInPlaza == 0)
        {
            capulatesInPlaza++;
            cout << "Capulet " << person << " enters the plaza" << endl;
            return sendto(socket, &ack_message, sizeof(char), 0, _client_addr, sizeof(sockaddr_storage));
        }
        else
        {
            // At least one montague is in the plaza so the capulet will have to wait until
            // the plaza is empty
            waitingCapulets.push_back(person);

            // snap-off a copy of the client's address on the heap to be used to notify the client
            // when it is able to enter the plaza
            sockaddr_storage * client_addr = new sockaddr_storage;
            memcpy(client_addr, _client_addr, sizeof(sockaddr_storage));
            capulet_client_addresses.insert(make_pair(person, client_addr));
        }
    }
    else
    {
        string message = "Invalid input. The family name is not recognized: " + family;
        assert(false && message.c_str());
    }

    return 0;
}

int HandleDepatureMessage(int socket, string person, string family)
{
    if (IsAMontague(family))
    {
        assert(montaguesInPlaza > 0 && L"There should be at least one montague in the plaza");
        montaguesInPlaza--;
        cout << "Montague " << person << " leaves the plaza" << endl;

        if (montaguesInPlaza == 0 && waitingCapulets.size() > 0)
        {
            // If no montagues are no longer in the plaza, allow all waiting capulets to
            // get in the plaza
            for (int i = 0; i < waitingCapulets.size(); i++)
            {
                string capulet = waitingCapulets[i];
                std::map<string, sockaddr_storage* >::iterator it = capulet_client_addresses.find(capulet);
                cout << "Capulet " << capulet << " enters the plaza" << endl;
                capulatesInPlaza++;
                int ret_val = sendto(socket, &ack_message, sizeof(char), 0, (struct sockaddr *) it->second, sizeof(sockaddr_storage));
                if (ret_val < 0)
                {
                    return ret_val;
                }

                delete it->second;
                capulet_client_addresses.erase(capulet);
            }

            waitingCapulets.empty();
            capulet_client_addresses.empty(); // should already be empty right before this call
        }
    }
    else if (IsACapulet(family))
    {
        assert(capulatesInPlaza > 0 && L"There should be at least one capulet in the plaza");
        capulatesInPlaza--;
        cout << "Capulet " << person << " leaves the plaza" << endl;

        if (capulatesInPlaza == 0 && waitingMontagues.size() > 0)
        {
            // If no capulets are no longer in the plaza, allow all waiting montagues to
            // get in the plaza
            for (int i = 0; i < waitingMontagues.size(); i++)
            {
                string montague = waitingMontagues[i];
                std::map<string, sockaddr_storage* >::iterator it = montague_client_addresses.find(montague);
                cout << "Montague " << montague << " enters the plaza" << endl;
                montaguesInPlaza++;
                int ret_val = sendto(socket, &ack_message, sizeof(char), 0, (struct sockaddr *) it->second, sizeof(sockaddr_storage));
                if (ret_val < 0)
                {
                    return ret_val;
                }

                delete it->second;
                montague_client_addresses.erase(montague);
            }

            waitingMontagues.empty();
            montague_client_addresses.empty(); // should already be empty right before this call
        }
    }
    else
    {
        string message = "Invalid input. The family name is not recognized: " + family;
        cout << message << endl;
        assert(false && message.c_str());
    }

    return 0;
}