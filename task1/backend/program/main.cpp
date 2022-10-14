#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <string.h>


#define PORT    1234
#define BROADCAST    "255.255.255.255"


void announceNumber(){
    int socketfd, rcv_ret;
    std::string addr;
    char reply[1024];
    addr = BROADCAST;
    int broadcastEnable=1;
    int len = sizeof(sockaddr_in);


    srand (time(NULL));
    int random_nr = rand() % 100 + 1;

    std::string msg = "election:" + std::to_string(random_nr);

    std::cout << "message to send: " <<msg << std::endl;

    auto iaddr = inet_addr(addr.c_str());

    struct sockaddr_in servaddr;

    if((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        std::cout << "Error creating socket";
    }

    int ret=setsockopt(socketfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&reply, 0, sizeof(reply));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = iaddr;

    std::cout << "sending message" << std::endl;

    sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

    std::cout << "message sent" << std::endl;
    close(socketfd);
}


std::string broadcastListen(){
  std::cout << "listen" << std::endl;
  int listeningSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
 // bind the port
    struct sockaddr_in sockaddr;
        char reply[1024];

    memset(&sockaddr, 0, sizeof(sockaddr));
    
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(1234);
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    std::cout << "gonna bind" << std::endl;
    int status = bind(listeningSocket, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (status == -1) {
            std::cout << "Error : " <<strerror(errno) << "  "<< errno <<std::endl;
    }
     // receive
    struct sockaddr_in receiveSockaddr;
    socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);
        std::cout << "gonna receive" << std::endl;

    ssize_t result = recvfrom(listeningSocket, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);

  std::cout << "happy" <<std::endl;
  return reply;
}



int main() {
    announceNumber();
    broadcastListen();
    return 0;
}
