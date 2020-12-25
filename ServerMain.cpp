#include <iostream>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <queue>
#include <map>
#include <vector>
#include <bits/stdc++.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

#define CLIENTS_THREAD_POOL_SIZE 50
#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

int portNumber;
int sock;
float PLP = 0;

struct clientArgs
{
    int windowSize;
    int base;
    int nextPacket;
    int congestionState;
    int ssthreshold;
    int duplicateAck;
    pthread_mutex_t mutex;
    pthread_cond_t condVar;
    pthread_mutex_t send_mutex;
    pthread_cond_t send_condVar;
    struct sockaddr_in clientAddress;
    vector<struct dataPacket> packets;
};

pthread_t clientsThreadPool[CLIENTS_THREAD_POOL_SIZE];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t clients_cond_var = PTHREAD_COND_INITIALIZER;
queue<int> ports;
map<int, string> files;
map<int, struct sockaddr_in> sockets;

struct dataPacket
{
    uint16_t checksum;
    uint16_t len;
    uint32_t seqno;
    bool fin;
    char data[500];
};

struct ackPacket
{
    uint16_t checksum;
    uint16_t len;
    uint32_t ackno;
};

void serveClient(int *port);
void *clientsFun(void *arg);
void *receiveAcks(void *arg);
bool corrupted(struct dataPacket packet);
void ackCheckSum(struct ackPacket *packet);
void dataCheckSum(struct dataPacket *packet);
void error(string err);
bool corruptedAck(struct ackPacket packet);

int main(int argc, char *argv[])
{
    if (argc != 2)
        error("Incorrect arguments!");
    string filePath = argv[1];

    for (int i = 0; i < CLIENTS_THREAD_POOL_SIZE; i++)
    {
        pthread_create(&clientsThreadPool[i], NULL, clientsFun, NULL);
    }

    fstream file;
    file.open(filePath, ios::in);
    if (file.is_open())
    {
        string line;
        int i = 0;
        while (getline(file, line))
        {
            switch (i)
            {
            case 0:
                portNumber = stoi(line);
                break;
            case 1:

                break;

            case 2:
                PLP = stoi(line);
                break;
            default:
                break;
            }
            i++;
        }
        file.close();
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        error("socket failed!");

    // int flags = fcntl(sock, F_GETFL, 0);
    // fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(portNumber);

    if ((bind(sock, (struct sockaddr *)&serverAddress, sizeof(serverAddress))) < 0)
    {
        error("bind failed");
    }
    while (1)
    {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrlen = sizeof(clientAddr);
        struct dataPacket request;
        ssize_t numBytes = recvfrom(sock, &request, sizeof(dataPacket), 0, (struct sockaddr *)&clientAddr, &clientAddrlen);
        if (numBytes > 0)
        {
            if (!corrupted(request) && request.len > 8)
            {
                string fileName = "";
                for (int i = 0; i < request.len - 8; i++)
                    fileName.push_back(request.data[i]);
                struct ackPacket ack;
                ack.len = 8;
                ack.ackno = request.seqno + request.len + 1;
                ackCheckSum(&ack);
                //sleep(5);
                // recvfrom(sock, &request, sizeof(dataPacket), 0, (struct sockaddr *)&clientAddr, &clientAddrlen);
                sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
                cout << "Acked" << endl;
                pthread_mutex_lock(&clients_mutex);
                ports.push(clientAddr.sin_port);
                files.insert(pair<int, string>(clientAddr.sin_port, (string)fileName));
                sockets.insert(pair<int, struct sockaddr_in>(clientAddr.sin_port, clientAddr));
                pthread_cond_signal(&clients_cond_var);
                pthread_mutex_unlock(&clients_mutex);
            }
        }
    }

    for (int i = 0; i < CLIENTS_THREAD_POOL_SIZE; i++)
    {
        pthread_join(clientsThreadPool[i], NULL);
    }
    return 0;
}

void error(string err)
{
    cout << err;
    exit(-1);
}

void *clientsFun(void *arg)
{
    while (1)
    {
        int *address = (int *)(malloc(sizeof(int)));
        int *port = (int *)(malloc(sizeof(int)));
        pthread_mutex_lock(&clients_mutex);
        if (ports.empty())
        {
            pthread_cond_wait(&clients_cond_var, &clients_mutex);
            *port = ports.front();
            ports.pop();
        }
        else
        {
            *port = ports.front();
            ports.pop();
        }
        pthread_mutex_unlock(&clients_mutex);
        serveClient(port);
    }
}

void serveClient(int *port)
{
    int clientPort = *port;
    auto itr = files.find(clientPort);
    string fileName = itr->second;
    string filePath = "server/" + fileName;
    auto itr2 = sockets.find(clientPort);
    struct sockaddr_in clientAddress = itr2->second;

    pthread_t receiveAcksThread;
    struct clientArgs args;
    args.base = 0;
    args.windowSize = 1;
    args.nextPacket = 0;
    args.mutex = PTHREAD_MUTEX_INITIALIZER;
    args.condVar = PTHREAD_COND_INITIALIZER;
    args.send_mutex = PTHREAD_MUTEX_INITIALIZER;
    args.send_condVar = PTHREAD_COND_INITIALIZER;
    args.clientAddress = clientAddress;
    args.congestionState = SLOW_START;
    pthread_create(&receiveAcksThread, NULL, receiveAcks, &args);

    ifstream file;
    file.open(filePath, ios::in);
    if (file.is_open())
    {
        int i = 0;
        int seq = 0;
        char data[500];
        struct dataPacket packet;
        while (!file.eof())
        {
            packet.data[i] = file.get();
            // cout << packet.data[i];
            i++;
            if (i == 500)
            {
                packet.fin = false;
                packet.seqno = seq;
                packet.len = 509;
                i = 0;
                seq++;
                dataCheckSum(&packet);
                args.packets.push_back(packet);
            }
        }
        file.close();
        // cout << i << endl;
        if (i != 0)
        {
            packet.fin = true;
            packet.seqno = seq;
            packet.len = 9 + i;
            seq++;
            dataCheckSum(&packet);
            args.packets.push_back(packet);
        }
    }

    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&args.mutex);
        if (args.windowSize > args.nextPacket - args.base &&
         args.nextPacket < args.packets.size())
        {
            sendto(sock, &args.packets[args.nextPacket], sizeof(dataPacket), 0, (struct sockaddr *)&clientAddress, sizeof(clientAddress));
            cout << args.nextPacket << " data sent" << endl;
            pthread_cond_signal(&args.send_condVar);
            args.nextPacket++;
        }
        pthread_mutex_unlock(&args.mutex);
    }
}

void *receiveAcks(void *arg)
{
    struct clientArgs *args = (struct clientArgs *)arg;
    socklen_t clientAddrlen = sizeof(args->clientAddress);

    while (1)
    {
        struct ackPacket ack;

        fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(sock, &set);
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        pthread_mutex_lock(&args->mutex);
        if (args->base == args->nextPacket)
        {
            pthread_mutex_unlock(&args->mutex);
            pthread_cond_wait(&args->send_condVar, &args->send_mutex);
        }
        else
            pthread_mutex_unlock(&args->mutex);
        int val = select(sock + 1, &set, NULL, NULL, &timeout);
        if (val < 0)
        {
            // cout << "heree" << endl;
        }
        else if (val == 0)
        {
            cout << "*******TIMEOUT********" << endl;
            pthread_mutex_lock(&args->mutex);
            args->ssthreshold = args->windowSize / 2;
            args->windowSize = 1;
            args->duplicateAck = 0;
            args->nextPacket = args->base;
            pthread_mutex_unlock(&args->mutex);
        }
        else
        {
            // cout << "receiving" << endl;
            ssize_t numBytes = recvfrom(sock, &ack, sizeof(ackPacket), 0, (struct sockaddr *)&args->clientAddress, &clientAddrlen);
            cout << ack.ackno << " " << ack.len << " " << args->nextPacket << endl;
            if (ack.len == 8 && !corruptedAck(ack) && ack.ackno <= args->base + args->windowSize)
            {
                cout << "recv" << endl;
                pthread_mutex_lock(&args->mutex);
                args->base = ack.ackno;
                cout << "base: " << args->base << endl;
                switch (args->congestionState)
                {
                case SLOW_START:
                    args->windowSize++;
                    if (args->windowSize >= args->ssthreshold)
                        args->congestionState = CONGESTION_AVOIDANCE;
                    cout << "window: " << args->windowSize << endl;
                    break;
                case CONGESTION_AVOIDANCE:

                    break;
                case FAST_RECOVERY:
                    break;
                }
                pthread_mutex_unlock(&args->mutex);
                cout << "ACKED " << ack.ackno << endl;
            }
            else
            {
            }
        }
    }
}

void dataCheckSum(struct dataPacket *packet)
{
}
void ackCheckSum(struct ackPacket *packet)
{
}
bool corrupted(struct dataPacket packet)
{
    return false;
}
bool corruptedAck(struct ackPacket packet)
{
    return false;
}