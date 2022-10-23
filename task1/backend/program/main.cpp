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
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

#define PORT 1234
#define BROADCAST "255.255.255.255"
#define NUM_THREADS 10
#define RAND 1000

int random_nr = 0;
bool master = false;
bool slave = false;
int reds = 0;
int greens = 0;
struct sockaddr_in receiveSockaddr;
socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);
long socketfd_global;
enum colors
{
  red = 0,
  green = 1
};
std::mutex node_vector_mutex;
std::mutex int_vector_mutex;
std::mutex socket_mutex;
struct node
{
  colors color;
  int node_nr;
  struct sockaddr_in node_addr;
};
std::vector<node> nodes = {};
std::vector<int> nodes_int = {};

void client(int random_nr)
{
  char reply[1024];
  int socketfd;
  std::string addr = BROADCAST;
  int broadcastEnable = 1;
  socklen_t len = sizeof(sockaddr_in);
  std::string msg = "ELECTION:" + std::to_string(random_nr) + '\0';
  auto iaddr = inet_addr(addr.c_str());
  struct sockaddr_in servaddr;

  if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    std::cout << "Socket error: " << strerror(errno) << "  " << errno << std::endl;
  }

  int ret = setsockopt(socketfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
  struct timeval tv;
  tv.tv_sec = 60;
  tv.tv_usec = 0;
  setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
  memset(&servaddr, 0, sizeof(servaddr));

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);
  servaddr.sin_addr.s_addr = iaddr;

  while (true)
  {
    // send broadcast, wait 60 seconds for reply and then repeat
    while (master == false && slave == false)
    {

      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);

      if (result > 0)
      {
        std::cout << "becoming slave, received: " << reply << std::endl;
        slave = true;
      }
    }

    // send ping periodically and check for color from master
    while (slave)
    { /*
       char buffer[INET_ADDRSTRLEN];
       inet_ntop(AF_INET, &servaddr.sin_addr, buffer, sizeof(buffer));*/
      msg = "PING:" + std::to_string(random_nr) + '\0';

      std::cout << "sending ping to master : " << msg << std::endl;
      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
      if (result < 0)
      {
        // master didnt reply, slave is free
        slave = false;
      }
      std::cout << "received from master: " << reply << std::endl;
      // sleep before next ping
      usleep(10000000);
    }
    usleep(100000000);
  }
}

void watchdog(node slave)
{
  long socketfd = socketfd_global;
  std::string msg = "orange";
  slave.color = red;
  socket_mutex.lock();
  sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&slave.node_addr, receiveSockaddrLen);
  socket_mutex.unlock();
  // add node to vector and increment color count
  node_vector_mutex.lock();
  if (slave.color == red)
  {
    reds++;
  }
  else if (slave.color == green)
  {
    greens++;
  }
  nodes.push_back(slave);
  node_vector_mutex.unlock();

  // loop to check if node is alive
  bool alive = true;
  while (alive)
  {
    usleep(30000000);
    // check if node has pinged since last time
    int_vector_mutex.lock();
    auto it = std::find(nodes_int.begin(), nodes_int.end(), slave.node_nr);
    if (it != nodes_int.end())
    {
      std::cout << "node: " << slave.node_nr << " alive" << std::endl;
      // node alive - erase from ping vector

      socket_mutex.lock();
      sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&slave.node_addr, receiveSockaddrLen);
      socket_mutex.unlock();
      nodes_int.erase(it);
    }
    else
    {
      // node dead
      alive = false;
    }
    int_vector_mutex.unlock();
  }
  std::cout << "node: " << slave.node_nr << " dead" << std::endl;

  // node dead, remove from vector
  node_vector_mutex.lock();
  int i = 0;
  bool found = false;
  for (i = 0; i < nodes.size(); i++)
  {
    if (nodes.at(i).node_nr == slave.node_nr)
    {
      break;
      found = true;
    }
  }
  if (found)
  {
    if (slave.color == red && reds > 0)
    {
      reds--;
    }
    else if (slave.color == green & greens > 0)
    {
      greens--;
    }
    nodes.erase(nodes.begin() + i);
    if (nodes.empty())
    {
      master = false;
    }
  }
  node_vector_mutex.unlock();
}
/*
void node_com(long socketfd)
{
  char reply[1024];

  char buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &receiveSockaddr.sin_addr, buffer, sizeof(buffer));
  std::cout << "sending hello3 to addr: " << buffer << std::endl;
  sendto(socketfd, "HELLO3", strlen("HELLO3"), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);

  recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
  std::cout << "reply after hello3: " << reply << std::endl;
}
*/
/*
void announceNumber(int random_nr)
{

  usleep(random_nr);

  char reply[1024];
  int socketfd;
  std::string addr = BROADCAST;
  int broadcastEnable = 1;
  socklen_t len = sizeof(sockaddr_in);
  // socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);

  std::string msg = "election:" + std::to_string(random_nr) + '\0';

  std::cout << "Message to seend thretatat: " << msg << std::endl;

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

  std::cout << "Seeending message" << std::endl;

  sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

  std::cout << "Message sent, closing socket" << std::endl;
  while (1)
  {
    ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
    // timeout or other error
    if (result < 0)
    {
      ;
    }
    std::cout << "DAFUQ " << reply << std::endl;
  }
}
*/
void process_reply(std::string reply_in, sockaddr_in address, long socketin)
{
  // threadArgs *arguments = (threadArgs*) arguments_in;
  // std::string str = arguments->reply;
  /*
  std::size_t pos = str.find("election:");
  std::string number_s = str.substr(pos + 9);
  int number_i = std::stoi(number_s);*/
  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  char reply[1024];

  socklen_t len = sizeof(sockaddr_in);

  std::cout << "processing reply: " << reply_in << std::endl;
  char buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  // auto iaddr = inet_addr(buffer);

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);
  servaddr.sin_addr.s_addr = address.sin_addr.s_addr;

  // std::cout << "addr " << buffer << std::endl;
  std::string msg = "REPLY TO ELECTION " + std::to_string(random_nr) + '\0';
  int socketfd;
  sendto(socketin, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
  recvfrom(socketin, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
  while (1)
    ;
  if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    std::cout << "Socket error: " << strerror(errno) << "  " << errno << std::endl;
  }
  // sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
}
/*void *waitForReply(void *arguments_in)
{
  threadArgs *arguments = (threadArgs *)arguments_in;
  long listeningSocket = arguments->listeningSocket;
  char reply[1024];
  struct sockaddr_in receiveSockaddr;
  socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);

  ssize_t result = recvfrom(listeningSocket, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);

  arguments->reply = reply;
}*/

void server()
{
  long socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  socketfd_global = socketfd;
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  char reply[1024];
  std::string msg;

  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(PORT);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int status = bind(socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (status == -1)
  {
    std::cout << "Bind error: " << strerror(errno) << "  " << errno << std::endl;
    close(socketfd);
  }

  while (slave == false)
  {
    std::cout << "listening" << std::endl;
    ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
    if (result < 0)
    {
      continue;
    }
    std::cout << "received message: " << reply << std::endl;
    std::string str = reply;
    memset(&reply, 0, sizeof(reply));

    std::size_t pos = str.find("ELECTION:");
    if (pos != std::string::npos)
    {
      // first contact with master
      std::string number_s = str.substr(pos + 9);
      int number_i = std::stoi(number_s);
      if (number_i != random_nr && slave == false)
      {
        master = true;
        msg = "WELCOME:" + std::to_string(random_nr) + '\0';

        socket_mutex.lock();
        sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
        socket_mutex.unlock();
        
        node slave;
        slave.node_addr = receiveSockaddr;
        slave.node_nr = number_i;
        std::thread t = std::thread(watchdog, slave);
        t.detach();
      }

      // continue to listen
      continue;
    }
    pos = str.find("PING:");
    if (pos != std::string::npos)
    {
      // ping
      std::string number_s = str.substr(pos + 5);
      int number_i = std::stoi(number_s);

      int_vector_mutex.lock();
      nodes_int.push_back(number_i);
      int_vector_mutex.unlock();

      // continue to listen
      continue;
    }
  }
}
/*
std::string init()
{
  std::vector<std::thread> threads;
  long listeningSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  threadArgs arguments;
  // arguments.listeningSocket = listeningSocket;
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));

  struct timeval tv;
  tv.tv_sec = 30;
  tv.tv_usec = 0;
  setsockopt(listeningSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(PORT);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int status = bind(listeningSocket, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (status == -1)
  {
    std::cout << "Bind error : " << strerror(errno) << "  " << errno << std::endl;
    close(listeningSocket);
  }

  char reply[1024];

  struct sockaddr_in receiveSockaddr;
  struct sockaddr_in masterSockaddr;

  socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);
  int highest_nr = random_nr;
  bool same_nr_flag = false;
  while (1)
  {
    ssize_t result = recvfrom(listeningSocket, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
    // timeout or other error
    if (result < 0)
    {
      break;
    }
    sendto(listeningSocket, "HELLO", strlen("HELLO"), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);

    std::thread th(process_reply, reply, receiveSockaddr, listeningSocket);
    threads.push_back(move(th));


        std::string str = reply;

        std::size_t pos = str.find("election:");
        std::string number_s = str.substr(pos + 9);
        int number_i = std::stoi(number_s);
        // store highest number and its corresponding address
        if (number_i > highest_nr)
        {
          std::cout << "old high " << highest_nr << " new high " << number_i << std::endl;
          highest_nr = number_i;
          masterSockaddr = receiveSockaddr;
        }
        // if received nr is smaller of this node, store received node address
        else if (number_i < random_nr)
        {
          std::cout << "adding: " << number_i << std::endl;
          nodes.push_back(receiveSockaddr);
        }
        else if (number_i == random_nr)
        {
          std::cout << "NUMBER SAME" << std::endl;
          same_nr_flag = true;
        }
  }
  for (unsigned int i = 0; i < threads.size(); ++i)
  {
    threads.at(i).join();
  }
  std::cout << "vector size " << nodes.size() << std::endl;
  return "";
}
*/

int main()
{
  srand(time(NULL));
  random_nr = rand() % RAND + 1;
  std::cout << random_nr << std::endl;
  std::thread t1(client, random_nr);
  usleep(1000000);
  std::thread t2(server);
  t1.detach();
  t2.detach();
  while (1)
    ;
}