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
#define RAND 1000000

int random_nr = 0;
bool master = false;
bool slave = false;
bool master_dead = false;
bool init = true;

int reds = 0;
int greens = 1;
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
std::mutex slave_init_mutex;
std::mutex master_init_mutex;
std::mutex master_dead_mutex;
std::mutex server_mutex;
std::mutex client_mutex;
std::mutex color_mutex;

#include <chrono>

using sysclock_t = std::chrono::system_clock;

std::string CurrentDate()
{
  std::time_t now = sysclock_t::to_time_t(sysclock_t::now());

  char buf[16] = {0};
  std::strftime(buf, sizeof(buf), "%T", std::localtime(&now));

  return std::string(buf);
}

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
  bool timeout = false;

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
    /*
    // check if not in master init phase
    master_init_mutex.lock();
    master_init_mutex.unlock();
    // slave init phase
    slave_init_mutex.lock();
    */
    slave = false;
    init = true;
    int count = 0;
    timeout = false;
    msg = "ELECTION:" + std::to_string(random_nr) + '\0';

    tv.tv_sec = 5;
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    std::cout << "SLAVE INIT BEG" << std::endl;

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = iaddr;

    // send broadcast, wait 30 seconds for reply
    while (init == true)
    {
      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);

      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);

      if (result > 0)
      {
        client_mutex.lock();
        slave = true;
        init = false;
      }
      else
      {
        count++;
        if (count > 10)
        {
          slave = false;
          init = false;
          usleep(10000000);

          std::cout << "SLAVE INIT END: becoming master, no reply" << std::endl;
        }
      }
    }

    while (slave)
    {
      servaddr.sin_family = AF_INET;
      servaddr.sin_port = htons(PORT);
      servaddr.sin_addr.s_addr = iaddr;

      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
      if (result > 0)
      {
        std::string str = reply;
        std::size_t pos = str.find("WELCOME:");
        if (pos != std::string::npos)
        {
          std::cout << "got WELCOME, becoming client" << std::endl;
          break;
        }
      }
      usleep(5000000);
    }

    tv.tv_sec = 30;
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    if (slave)
    {
      // send ping periodically and check for color from master
      while (true)
      {
        msg = "PING:" + std::to_string(random_nr);

        std::cout << "sending ping to master : " << msg << std::endl;
        sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
        ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
        if (result < 0)
        {
          // master didnt reply, slave is free -> unlock mutex
          usleep(10000000);
          std::cout << "slave is FREE -----------------"
                    << std::endl;
          client_mutex.unlock();
          break;
        }
        else
        {
          std::cout << "received from master: " << reply << std::endl;
        }
        memset(&reply, 0, sizeof(reply));

        // sleep before next ping
        usleep(5000000);
      }
    }

    usleep(random_nr * 50);
  }
}

void watchdog(node slave)
{
  usleep(1000000);
  long socketfd = socketfd_global;
  std::string msg;
  slave.color = red;
  bool slave_green = false;
  std::cout << "watchdog adding node: " << slave.node_nr << std::endl;
  // add node to vector and increment color count
  node_vector_mutex.lock();

  color_mutex.lock();

  int total = greens + reds + 1;
  int desired_greens;
  std::cout << "total: " << total << " reds " << reds << " greens " << greens << std::endl;

  if (total % 3 == 0)
  {
    desired_greens = total / 3;
    if (greens == desired_greens)
    {
      slave_green = false;
    }
    else
    {
      slave_green = true;
    }
  }
  else
  {
    desired_greens = (total + 3) / 3;
    if (greens == desired_greens)
    {
      slave_green = false;
    }
    else
    {
      slave_green = true;
    }
  }

  if (slave_green)
  {
    slave.color = green;
    greens++;
  }
  else
  {
    slave.color = red;
    reds++;
  }

  color_mutex.unlock();

  nodes.push_back(slave);
  node_vector_mutex.unlock();

  // loop to check if node is alive
  bool alive = true;
  while (alive)
  {
    for (int i = 0; i < nodes.size(); i++)
    {
      if (nodes.at(i).node_nr == slave.node_nr)
      {
        slave = nodes.at(i);
        break;
      }
    }
    usleep(15000000);
    // check if node has pinged since last time
    if (slave.color == green)
    {
      msg = "GREEN" + std::to_string(slave.node_nr);
    }
    else if (slave.color == red)
    {
      msg = "RED" + std::to_string(slave.node_nr);
    }
    int_vector_mutex.lock();
    auto it = std::find(nodes_int.begin(), nodes_int.end(), slave.node_nr);
    if (it != nodes_int.end())
    {
      std::cout << "node: " << slave.node_nr << " alive and " << slave.color << std::endl;
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
  
  std::cout << "node: " << slave.node_nr << " dead"    << std::endl;

  // node dead, remove from vector
  node_vector_mutex.lock();
  int i = 0;
  bool found = false;
  for (i = 0; i < nodes.size(); i++)
  {
    if (nodes.at(i).node_nr == slave.node_nr)
    {
      found = true;
      break;
    }
  }
  if (found)
  {
    std::cout << "erasing node: " << slave.node_nr << std::endl;
    if (slave.color == red)
    {
      reds--;
    }
    else if (slave.color == green)
    {
      greens--;
    }
    nodes.erase(nodes.begin() + i);

    int total = greens + reds;
    int desired_greens;
    bool make_green = false;
    bool make_red = false;
    std::cout << "total " << total << " greens " << greens << " reds " << reds << std::endl;
    if (total % 3 == 0)
    {
      desired_greens = total / 3;
      std::cout << "desired greens " << desired_greens << std::endl;

      if (greens < desired_greens)
      {
        make_green = true;
        std::cout << "make green" << std::endl;
      }
      else if (greens > desired_greens)
      {
        make_red = true;
        std::cout << "make red" << std::endl;
      }
    }
    else
    {
      desired_greens = (total + 3) / 3;
      std::cout << "desired greens " << desired_greens << std::endl;

      if (greens < desired_greens)
      {
        make_green = true;
        std::cout << "make green" << std::endl;
      }
      else if (greens > desired_greens)
      {
        make_red = true;
        std::cout << "make red" << std::endl;
      }
    }
    if (make_green)
    {
      for (i = 0; i < nodes.size(); i++)
      {
        if (nodes.at(i).color == red)
        {
          std::cout << "found red node: " << nodes.at(i).node_nr << std::endl;
          nodes.at(i).color = green;
          std::cout << "node new color " << nodes.at(i).color << std::endl;

          break;
        }
      }
    }
    else if (make_red)
    {
      for (i = 0; i < nodes.size(); i++)
      {
        if (nodes.at(i).color == green)
        {
          std::cout << "found green node: " << nodes.at(i).node_nr << std::endl;
          nodes.at(i).color = red;
          std::cout << "node new color " << nodes.at(i).color << std::endl;

          break;
        }
      }
    }
  }
  node_vector_mutex.unlock();
}

void server()
{
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  char reply[1024];
  std::string msg;
  long socketfd = 0;
  std::vector<node> slaves_to_be;
  std::vector<int> my_slaves;

  while (true)
  {
    // dont start master init if node became slave
    client_mutex.lock();
    client_mutex.unlock();

    socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    socketfd_global = socketfd;
   
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(PORT);
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int status = bind(socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (status == -1)
    {
      std::cout << "Bind error: " << strerror(errno) << "  " << errno << std::endl;
      close(socketfd);
    }

    while (init)
    {
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
      if (result > 0)
      {
        std::string str = reply;
        memset(&reply, 0, sizeof(reply));

        std::size_t pos = str.find("ELECTION:");
        if (pos != std::string::npos)
        {
          std::string number_s = str.substr(pos + 9);
          int number_i = std::stoi(number_s);
          if (number_i < random_nr)
          {
            // number is smaller, reply and keep listening
            msg = "HELLO:" + std::to_string(random_nr);

            socket_mutex.lock();

            char buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &receiveSockaddr.sin_addr, buffer, sizeof(buffer));

            node slave_for_later;
            slave_for_later.node_addr = receiveSockaddr;
            slave_for_later.node_nr = number_i;
            slaves_to_be.push_back(slave_for_later);

            std::cout << "sending message back to addr: " << buffer << std::endl;
            sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
            socket_mutex.unlock();
          }
          else
          {
            // number is bigger, can't become master -> stop server
            slave == true;
          }
        }
        pos = str.find("PING:");
        if (pos != std::string::npos)
        {
          msg = "GOTCHA:" + std::to_string(random_nr);
          socket_mutex.lock();
          sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
          socket_mutex.unlock();
        }
      }
    }

    while (slave == false)
    {
      if (!master)
      {
        std::cout << "MASTER BEG"
                  << std::endl;
      }
      for (int j = 0; j < slaves_to_be.size(); j++)
      {
        node temp = slaves_to_be.at(j);
        auto it = std::find(my_slaves.begin(), my_slaves.end(), temp.node_nr);
        if (it == my_slaves.end())
        {
          my_slaves.push_back(temp.node_nr);
          std::thread t = std::thread(watchdog, temp);
          t.detach();
        }
      }
      for (int j = 0; j < slaves_to_be.size(); j++)
      {
        slaves_to_be.erase(slaves_to_be.begin() + j);
      }
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
      if (result < 0)
      {
        // stop master init phase if no reply received in 120 seconds -> unlock mutex
        std::cout << "MASTER INIT END: no election msg received"
                  << std::endl;
        break;
      }
      std::cout << "master received message: " << reply << std::endl;
      std::string str = reply;
      memset(&reply, 0, sizeof(reply));

      std::size_t pos = str.find("ELECTION:");
      if (pos != std::string::npos)
      {
        // first contact with master
        std::string number_s = str.substr(pos + 9);
        int number_i = std::stoi(number_s);
        if (number_i != random_nr)
        {
          // when node becomes master - no need to unlock mutex because node can never leave this phase
          if (!master)
          {
            std::cout << "MASTER INIT END: becoming master"
                      << std::endl;
          }

          master = true;
          msg = "WELCOME:" + std::to_string(random_nr) + '\0';

          socket_mutex.lock();

          char buffer[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &receiveSockaddr.sin_addr, buffer, sizeof(buffer));

          std::cout << "sending message back to addr: " << buffer << std::endl;
          sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
          socket_mutex.unlock();

          node slave;
          slave.node_addr = receiveSockaddr;
          slave.node_nr = number_i;
          auto it = std::find(my_slaves.begin(), my_slaves.end(), slave.node_nr);
          if (it == my_slaves.end())
          {
            my_slaves.push_back(slave.node_nr);
            std::thread t = std::thread(watchdog, slave);
            t.detach();
          }
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
    close(socketfd);

    // master_init_mutex.unlock();

    usleep(10000000);
  }
}

int main()
{
  srand(time(NULL));
  random_nr = rand() % RAND;
  std::cout << random_nr << std::endl;
  std::thread t1(client, random_nr);
  usleep(1000000);
  std::thread t2(server);
  t1.detach();
  t2.detach();
  while (1)
    ;
}