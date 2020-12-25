#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <vector>

using namespace std;

string serverAddress;
int serverPort;
string fileName;
vector<struct dataPacket> packets;

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

bool corrupted(struct dataPacket packet);
bool corruptedAck(struct ackPacket packet);
void ackCheckSum(struct ackPacket *packet);
void dataCheckSum(struct dataPacket *packet);
void error(string err);

int main(int argc, char *argv[])
{
    if (argc != 2)
        error("Incorrect arguments!");

    string filePath = argv[1];
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
                serverAddress = line;
                break;
            case 1:
                serverPort = stoi(line);
                break;
            case 2:
                fileName = line;
                break;
            default:
                break;
            }
            i++;
        }
        file.close();
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        error("socket failed");
    }
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(serverPort);

    struct dataPacket request;
    request.len = 8 + fileName.length();
    request.seqno = 1;
    strcpy(request.data, fileName.c_str());
    dataCheckSum(&request);

    socklen_t serverAddrlen = sizeof(serverAddress);
    struct ackPacket ack;
    bool timedout = true;

    while (timedout)
    {
        sendto(sock, &request, sizeof(request), 0, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
        cout << "SENT" << endl;

        fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(sock, &set);
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;
        int val = select(sock + 1, &set, NULL, NULL, &timeout);
        if (val < 0)
        {
            timedout = true;
        }
        else if (val == 0)
        {
            cout << "*********TIMEOUT**********" << endl;
            timedout = true;
        }
        else
        {
            ssize_t numBytes = recvfrom(sock, &ack, sizeof(ackPacket), 0, (struct sockaddr *)&serverAddress, &serverAddrlen);
            cout << ack.ackno << " " << ack.len << endl;
            if (ack.len == 8 && !corruptedAck(ack))
            {
                cout << "ACKED " << ack.ackno << endl;
                timedout = false;
            }
            else
            {
                timedout = true;
            }
        }
    }
    int expectedSeq = 0;
    while (true)
    {
        struct dataPacket packet;
        socklen_t serverAddrlen = sizeof(serverAddress);
        ssize_t numBytes = recvfrom(sock, &packet, sizeof(dataPacket), 0, (struct sockaddr *)&serverAddress, &serverAddrlen);
        // cout << numBytes << endl;
        if (numBytes > 0)
        {
            cout << packet.seqno << " " << expectedSeq << endl;
            if (packet.seqno == expectedSeq && !corrupted(packet))
            {
                expectedSeq++;

                struct ackPacket ack;
                ack.len = 8;
                ack.ackno = expectedSeq;
                ackCheckSum(&ack);
                // sleep(2);
                sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
                cout << "sent " << ack.ackno << endl;
                packets.push_back(packet);

                if (packet.fin)
                {
                    cout << "fin" << endl;
                    ofstream outputFile(fileName, std::ios_base::app);
                    for (int j = 0; j < packets.size(); j++)
                    {
                        struct dataPacket packet = packets[j];
                        for (int i = 0; i < packet.len - 10; i++)
                        {
                            outputFile << packet.data[i];
                            // cout << packet.data[i];
                        }
                    }
                    outputFile.close();
                }
            }
            else
            {
                struct ackPacket ack;
                ack.len = 8;
                ack.ackno = expectedSeq;
                ackCheckSum(&ack);
                // sleep(2);
                sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
                cout << "sent duplicate " << ack.ackno << endl;
            }
        }
    }
    return 0;
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
void error(string err)
{
    cout << err;
    exit(-1);
}