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
#define RAND 10000

int random_nr = 0;
bool master = false;
bool slave = false;
bool master_dead = false;
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
std::mutex init_mutex;
std::mutex master_dead_mutex;
std::mutex server_mutex;
std::mutex client_mutex;

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
  tv.tv_sec = 30;
  tv.tv_usec = 0;
  setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
  memset(&servaddr, 0, sizeof(servaddr));

  while (true)
  {
    // std::cout << "beginning of loop" << std::endl;

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = iaddr;
    // send broadcast, wait 60 seconds for reply and then repeat
    while (master == false && slave == false)
    {
      init_mutex.lock();
      master_dead_mutex.unlock();
      std::cout << "lock mutex" << std::endl;
      std::cout << "BROADCAST and look for master" << std::endl;
      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
      if (result > 0)
      {
        std::cout << "becoming slave, received: " << reply << std::endl;
        memset(&reply, 0, sizeof(reply));
        client_mutex.lock();
        slave = true;
        master_dead = false;
      }
      init_mutex.unlock();
      std::cout << "unlock mutex" << std::endl;
      usleep(1000000);
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
        master_dead = true;
        master_dead_mutex.lock();
        client_mutex.unlock();
        std::cout << "slave is free" << std::endl;
      }
      std::cout << "received from master: " << reply << std::endl;
      memset(&reply, 0, sizeof(reply));

      // sleep before next ping
      usleep(10000000);
    }
    // std::cout << "end of loop" << std::endl;

    usleep(10000000);
  }
}

void watchdog(node slave)
{
  long socketfd = socketfd_global;
  std::string msg = "orange" + std::to_string(random_nr);
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
    usleep(15000000);
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

void server()
{
  std::cout << "starting server " << std::endl;
  long socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  socketfd_global = socketfd;
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  char reply[1024];
  std::string msg;

   /*struct timeval tv;
  tv.tv_sec = 60;
  tv.tv_usec = 0;
  setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
*/
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(PORT);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int status = bind(socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (status == -1)
  {
    std::cout << "Bind error: " << strerror(errno) << "  " << errno << std::endl;
    close(socketfd);
  }
  while (true)
  {
    std::cout << "waiting for mutex " << std::endl;
    master_dead_mutex.lock();
    init_mutex.lock();
    client_mutex.lock();
    std::cout << "server mutex" << std::endl;
    client_mutex.unlock();
    init_mutex.unlock();
    master_dead_mutex.unlock();

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
    usleep(10000000);
    /*if (master_dead == true)
    {
      usleep(60000000);
    }*/
  }
}

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