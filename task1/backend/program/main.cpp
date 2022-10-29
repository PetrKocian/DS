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
#define RAND 1000000

// enables additional print statements
#define DEBUG 0

int random_nr = 0;
bool master = false;
bool slave = false;
bool init = true;

long socketfd_global;

// mutexes for thread sync and writing to shared resources
std::mutex node_vector_mutex;
std::mutex int_vector_mutex;
std::mutex socket_mutex;
std::mutex client_mutex;
std::mutex color_mutex;
std::mutex log_mutex; //mutex for std::cout so that watchdog threads don't mix messages in logs

enum colors
{
  red = 0,
  green = 1
};
int reds = 0;
int greens = 1; // master is green

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
  master = false;

  // socket creation and settings - broadcast and timeout
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

  // init and client loop, once a node becomes master it stays master forever -> client thread ends
  while (master == false)
  {
#if (DEBUG)
    std::cout << "SLAVE INIT BEG" << std::endl;
#endif

    slave = false;
    init = true;
    int count = 0;
    msg = "ELECTION:" + std::to_string(random_nr) + '\0';

    // decrease timeout settings
    tv.tv_sec = 5;
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = iaddr;

    // init loop
    while (init == true)
    {
      // send broadcast and wait for reply
      sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);

      // reply received -> there is a node with higher number, end init and become slave
      if (result > 0)
      {
        memset(&reply, 0, sizeof(reply));
        // lock client mutex to block server/master thread
        client_mutex.lock();
        std::cout << "INIT end, becoming slave" << std::endl;
        slave = true;
        init = false;
      }
      // timeout -> send another broadcast and wait until 10 timeouts, then become master
      else
      {
        count++;
        if (count > 10)
        {
          slave = false;
          init = false;
          master = true;
          std::cout << "INIT end, becoming master, color: GREEN" << std::endl;
          usleep(10000000);
        }
      }
    }

    // wait in this loop until master is decided
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
        memset(&reply, 0, sizeof(reply));
        std::size_t pos = str.find("WELCOME:");
        if (pos != std::string::npos)
        {
#if (DEBUG)
          std::cout << "got WELCOME, becoming client" << std::endl;
#endif
          break;
        }
      }
      usleep(5000000);
    }

    // increase timeout settings
    tv.tv_sec = 30;
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    if (slave)
    {
      // send ping periodically and check for color from master
      while (true)
      {
        msg = "PING:" + std::to_string(random_nr);

#if (DEBUG)
        std::cout << "sending ping to master : " << msg << std::endl;
#endif
        sendto(socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&servaddr, len);
        ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&servaddr, &len);
        if (result < 0)
        {
          // master didnt reply, slave is free -> unlock client mutex
          std::cout << "Master dead, start INIT" << std::endl;
          client_mutex.unlock();
          break;
        }
        else
        {
          std::string str = reply;
          std::size_t pos = str.find("WELCOME:");
          // ignore WELCOME messages for other slaves and print my color
          if (pos == std::string::npos)
          {
            std::cout << "I'm: " << reply << std::endl;
          }
        }
        memset(&reply, 0, sizeof(reply));

        // sleep before next ping
        usleep(5000000);
      }
    }
    // sleep before next init based on random_nr
    usleep(random_nr * 50);
  }
}

void watchdog(node slave)
{
  // sleep to allow master to create all watchdogs in first iteration of master loop
  usleep(1000000);
  long socketfd = socketfd_global;
  socklen_t receiveSockaddrLen = sizeof(slave.node_addr);
  std::string msg;
  slave.color = red;
  bool slave_green = false;
#if (DEBUG)
  std::cout << "watchdog adding node: " << slave.node_nr << std::endl;
#endif
  // lock shared resources
  node_vector_mutex.lock();
  color_mutex.lock();

  // total is +1 because this slave is not accounted for yet
  int total = greens + reds + 1;
  int desired_greens;
#if (DEBUG)
  std::cout << "total: " << total << " reds " << reds << " greens " << greens << std::endl;
#endif

  // compute desired number of green nodes and make node green if higher than now green nodes
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
  // increment color counts, add node to vector and release mutexes
  if (slave_green)
  {
    slave.color = green;
    msg = "GREEN ";
    greens++;
  }
  else
  {
    slave.color = red;
    msg = "RED ";
    reds++;
  }
  color_mutex.unlock();
  nodes.push_back(slave);
  node_vector_mutex.unlock();

  //log print
  log_mutex.lock();
  std::cout << "Adding slave: " << slave.node_nr << " color: " << msg << std::endl;
  log_mutex.unlock();
  //send first message to slave
  socket_mutex.lock();
  sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&slave.node_addr, receiveSockaddrLen);
  socket_mutex.unlock();

  // loop to check if node is alive
  bool alive = true;
  while (alive)
  {
    //need to update node info
    for (int i = 0; i < nodes.size(); i++)
    {
      if (nodes.at(i).node_nr == slave.node_nr)
      {
        slave = nodes.at(i);
        break;
      }
    }
    //print node info
    if (slave.color == green)
    {
      msg = "GREEN";
      log_mutex.lock();
      std::cout << "Slave: " << slave.node_nr << " alive and GREEN" << std::endl;
      log_mutex.unlock();
    }
    else if (slave.color == red)
    {
      log_mutex.lock();
      msg = "RED";
      std::cout << "Slave: " << slave.node_nr << " alive and RED" << std::endl;
      log_mutex.unlock();
    }

    usleep(15000000);
    // check if node has pinged since last time
    int_vector_mutex.lock();
    auto it = std::find(nodes_int.begin(), nodes_int.end(), slave.node_nr);
    if (it != nodes_int.end())
    {
      // node alive - erase from ping vector and send message with its color
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

  log_mutex.lock();
  std::cout << "Slave: " << slave.node_nr << " DEAD" << std::endl;
  log_mutex.unlock();

  // node dead, remove from vector and decrease color count
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
#if (DEBUG)
    std::cout << "erasing node: " << slave.node_nr << std::endl;
#endif
    if (slave.color == red)
    {
      reds--;
    }
    else if (slave.color == green)
    {
      greens--;
    }
    nodes.erase(nodes.begin() + i);

    //need to check if color ratio has been disrupted
    int total = greens + reds;
    int desired_greens;
    bool make_green = false;
    bool make_red = false;
#if (DEBUG)
    std::cout << "total " << total << " greens " << greens << " reds " << reds << std::endl;
#endif
    if (total % 3 == 0)
    {
      desired_greens = total / 3;
#if (DEBUG)
      std::cout << "desired greens " << desired_greens << std::endl;
#endif

      if (greens < desired_greens)
      {
        make_green = true;
#if (DEBUG)
        std::cout << "make green" << std::endl;
#endif
      }
      else if (greens > desired_greens)
      {
        make_red = true;
#if (DEBUG)
        std::cout << "make red" << std::endl;
#endif
      }
    }
    else
    {
      desired_greens = (total + 3) / 3;
#if (DEBUG)
      std::cout << "desired greens " << desired_greens << std::endl;
#endif

      if (greens < desired_greens)
      {
        make_green = true;
#if (DEBUG)
        std::cout << "make green" << std::endl;
#endif
      }
      else if (greens > desired_greens)
      {
        make_red = true;
#if (DEBUG)
        std::cout << "make red" << std::endl;
#endif
      }
    }
    // if color change is needed, find node in vector and change its color -> its watchdog will send it update with next ping
    if (make_green)
    {
      for (i = 0; i < nodes.size(); i++)
      {
        if (nodes.at(i).color == red)
        {
          nodes.at(i).color = green;
          log_mutex.lock();
          std::cout << "Slave " << nodes.at(i).node_nr << " is now GREEN" << std::endl;
          log_mutex.unlock();
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
          nodes.at(i).color = red;
          log_mutex.lock();
          std::cout << "Slave " << nodes.at(i).node_nr << " is now RED" << std::endl;
          log_mutex.unlock();
          break;
        }
      }
    }
  }
  node_vector_mutex.unlock();
}

void server()
{
  struct sockaddr_in receiveSockaddr;
  socklen_t receiveSockaddrLen = sizeof(receiveSockaddr);
  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  char reply[1024];
  std::string msg;
  long socketfd = 0;
  std::vector<node> slaves_to_be;
  std::vector<int> my_slaves;

  while (true)
  {
    // dont start master loop if node became slave
    client_mutex.lock();
    client_mutex.unlock();

    //socket creation and bind
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

    //init loop - client thread sets init = 0 when init phase ends
    while (init)
    {
      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
      if (result > 0)
      {
        std::string str = reply;
        memset(&reply, 0, sizeof(reply));
        //if message is election -> check node random_nr size
        std::size_t pos = str.find("ELECTION:");
        if (pos != std::string::npos)
        {
          std::string number_s = str.substr(pos + 9);
          int number_i = std::stoi(number_s);
          if (number_i < random_nr)
          {
            // number is smaller, save node as slave, reply and keep listening
            msg = "HELLO:" + std::to_string(random_nr);

            socket_mutex.lock();
#if (DEBUG)
            char buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &receiveSockaddr.sin_addr, buffer, sizeof(buffer));
#endif
            node slave_for_later;
            slave_for_later.node_addr = receiveSockaddr;
            slave_for_later.node_nr = number_i;
            slaves_to_be.push_back(slave_for_later);

#if (DEBUG)
            std::cout << "sending message back to addr: " << buffer << std::endl;
#endif
            sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
            socket_mutex.unlock();
          }
          else
          {
            // number is bigger, this node can't become master -> stop master loop
            slave == true;
          }
        }
        // if message is ping, reply to node to keep it from timing out
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
    //main master loop
    while (slave == false)
    {
      //create 1 watchdog thread for each slave
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
      //clear slaves_to_be to prevent watchdog duplication in next loop iterations
      for (int j = 0; j < slaves_to_be.size(); j++)
      {
        slaves_to_be.erase(slaves_to_be.begin() + j);
      }

      ssize_t result = recvfrom(socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);
      if (result < 0)
      {
        //break loop on recvfrom error
        break;
      }
#if (DEBUG)
      std::cout << "master received message: " << reply << std::endl;
#endif
      std::string str = reply;
      memset(&reply, 0, sizeof(reply));
      //check for new nodes
      std::size_t pos = str.find("ELECTION:");
      if (pos != std::string::npos)
      {
        // new node - first contact with master
        std::string number_s = str.substr(pos + 9);
        int number_i = std::stoi(number_s);
        if (number_i != random_nr)
        {
          msg = "WELCOME:" + std::to_string(random_nr);

          socket_mutex.lock();

          char buffer[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &receiveSockaddr.sin_addr, buffer, sizeof(buffer));

#if (DEBUG)
          std::cout << "sending message back to addr: " << buffer << std::endl;
#endif
          sendto(socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
          socket_mutex.unlock();

          //create slave watchdog if not yet in my slaves
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
      //check for ping from slaves
      pos = str.find("PING:");
      if (pos != std::string::npos)
      {
        // ping -> add node to ping vector, watchdog then reads and removes it
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
    usleep(10000000);
  }
}

int main()
{
  //init random number, start client and server thread, then wait in infinite loop
  srand(time(NULL));
  random_nr = rand() % RAND;
  std::cout << std::endl
            << random_nr << std::endl
            << std::endl;
  std::cout << "INIT" << std::endl;
  std::thread t1(client, random_nr);
  usleep(1000000);
  std::thread t2(server);
  t1.detach();
  t2.detach();
  while (1)
    ;
}