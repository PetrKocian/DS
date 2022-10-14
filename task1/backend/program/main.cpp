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
#include <pthread.h>
#include <unistd.h>

#define PORT 1234
#define BROADCAST "255.255.255.255"
#define NUM_THREADS 10
#define RAND 1000

struct threadArgs
{
  long listeningSocket;
  std::string reply = "";
};

void announceNumber(int random_nr)
{

  usleep(random_nr);

  int socketfd;
  std::string addr = BROADCAST;
  int broadcastEnable = 1;
  int len = sizeof(sockaddr_in);

  std::string msg = "election:" + std::to_string(random_nr);

  std::cout << "Message to send: " << msg << std::endl;

  auto iaddr = inet_addr(addr.c_str());

  struct sockaddr_in servaddr;

  if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    std::cout << "Socket error : " << strerror(errno) << "  " << errno << std::endl;
  }

  int ret = setsockopt(socketfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

  memset(&servaddr, 0, sizeof(servaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);
  servaddr.sin_addr.s_addr = iaddr;

  std::cout << "Sending message" << std::endl;

  sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

  std::cout << "Message sent, closing socket" << std::endl;

  close(socketfd);
}

void *waitForReply(void *arguments_in)
{
  threadArgs *arguments = (threadArgs *)arguments_in;
  long listeningSocket = arguments->listeningSocket;
  char reply[1024];
  struct sockaddr_in receiveSockaddr;
  socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);

  ssize_t result = recvfrom(listeningSocket, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);

  arguments->reply = reply;
}

std::string broadcastListen()
{
  pthread_t threads[NUM_THREADS];
  int i = 0;
  long listeningSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  threadArgs arguments;
  arguments.listeningSocket = listeningSocket;
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));

  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(PORT);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int status = bind(listeningSocket, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (status == -1)
  {
    std::cout << "Bind error : " << strerror(errno) << "  " << errno << std::endl;
    close(listeningSocket);
  }

  /*while (1)
  {
    pthread_create(&threads[i++], NULL, waitForReply, (void *)&arguments);
    std::cout << "creating thread" << std::endl;
    // if there are already too many threads running, wait until they finish
    if (i >= NUM_THREADS)
    {
      std::cout << "too many threads" << std::endl;

      i = 0;
      // wait for threads to finish once we reach 10, then start from 0 again
      while (i < NUM_THREADS)
      {
        std::cout << "waiting for threads: " << i << std::endl;

        // std::cout << "Waiting for thread: " << i << std::endl;
        pthread_join(threads[i++], NULL);
        std::cout << "thread: " << arguments.reply << std::endl;
        // std::cout << "Thread: " << i-1 << " finished" << std::endl;
      }
      i = 0;
    }
  }*/

  char reply[1024];

  struct sockaddr_in receiveSockaddr;
  socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);
  while (1)
  {
    ssize_t result = recvfrom(listeningSocket, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
    std::cout << reply << std::endl;
  }

  return "";
}

int main()
{

  srand(time(NULL));
  int random_nr = rand() % RAND + 1;

  announceNumber(random_nr);
  broadcastListen();
  return 0;
}
