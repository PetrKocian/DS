#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <string.h>
#include <vector>
#include <thread>
#include <stdlib.h>
#include <mutex>
#include <algorithm>
#include <Python.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <map>

#define PORT 1234

bool root_flag = false;

// key value store
std::map<std::string, std::string> data;

// reply buffer
char reply[1024];

// global sockets
int server_socketfd;
int parent_socketfd;
struct sockaddr_in receiveSockaddr;
socklen_t receiveSockaddrLen = sizeof(sockaddr_in);
struct sockaddr_in parent_sockaddr;
socklen_t parentSockaddrLen = sizeof(sockaddr_in);

// address of relevant nodes
std::string root_addr;
std::string node_addr;
std::string parent_addr;
int tree_depth;

// calls a python function which sends a request to zookeeper to create root node with specified address
// reason: kazoo library in python
void register_root(std::string root_addr)
{
  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;
  setenv("PYTHONPATH", ".", 1);

  Py_Initialize();
  pName = PyUnicode_DecodeFSDefault("zoo");

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL)
  {
    pFunc = PyObject_GetAttrString(pModule, "register_root");

    if (pFunc && PyCallable_Check(pFunc))
    {
      pArgs = PyTuple_Pack(1, PyUnicode_FromString(root_addr.c_str()));
      pValue = PyObject_CallObject(pFunc, pArgs);
      Py_DECREF(pArgs);
      if (pValue != NULL)
      {
        auto result = _PyUnicode_AsString(pValue);
        std::cout << "ROOT REGISTER RESULT: " << result << std::endl;
        Py_DECREF(pValue);
        return;
      }
      else
      {
        Py_DECREF(pFunc);
        Py_DECREF(pModule);
        PyErr_Print();
        fprintf(stderr, "Call failed\n");
        return;
      }
    }
    else
    {
      if (PyErr_Occurred())
        PyErr_Print();
      fprintf(stderr, "Cannot find function \"get_parent\"\n");
    }
    Py_XDECREF(pFunc);
    Py_DECREF(pModule);
  }
  else
  {
    PyErr_Print();
    fprintf(stderr, "Failed to load \"zoo\"\n");
    return;
  }
  if (Py_FinalizeEx() < 0)
  {
    return;
  }
  return;
}

// calls a python function which sends a request to zookeeper to create a node with specified address and return parent address
// reason: kazoo library in python
std::string get_parent_zk(std::string node_addr, std::string root_addr, int tree_depth)
{
  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;
  setenv("PYTHONPATH", ".", 1);

  Py_Initialize();
  pName = PyUnicode_DecodeFSDefault("zoo");

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL)
  {
    pFunc = PyObject_GetAttrString(pModule, "get_parent");

    if (pFunc && PyCallable_Check(pFunc))
    {
      pArgs = PyTuple_Pack(3, PyUnicode_FromString(node_addr.c_str()), PyUnicode_FromString(root_addr.c_str()), PyLong_FromLong(tree_depth));
      pValue = PyObject_CallObject(pFunc, pArgs);
      Py_DECREF(pArgs);
      if (pValue != NULL)
      {
        auto result = _PyUnicode_AsString(pValue);
        Py_DECREF(pValue);
        return result;
      }
      else
      {
        Py_DECREF(pFunc);
        Py_DECREF(pModule);
        PyErr_Print();
        fprintf(stderr, "Call failed\n");
        return "";
      }
    }
    else
    {
      if (PyErr_Occurred())
        PyErr_Print();
      fprintf(stderr, "Cannot find function \"get_parent\"\n");
    }
    Py_XDECREF(pFunc);
    Py_DECREF(pModule);
  }
  else
  {
    PyErr_Print();
    fprintf(stderr, "Failed to load \"zoo\"\n");
    return "";
  }
  if (Py_FinalizeEx() < 0)
  {
    return "";
  }
  return "";
}

// processes a received message and sends appropriate reply
void process_message(std::string rcv_msg)
{
  // GET_PARENT message from node
  auto pos = rcv_msg.find("GET_PARENT ");
  if (pos != std::string::npos && root_flag)
  {
    pos = rcv_msg.find(" ");
    rcv_msg = rcv_msg.substr(pos + 1);

    // call get parent at zookeeper
    std::string node_parent = get_parent_zk(rcv_msg, node_addr, tree_depth);

    // return parent address to node
    std::string msg = "PARENT " + node_parent;
    std::cout << msg << std::endl;
    sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
    return;
  }
  // GET message from node/client
  pos = rcv_msg.find("GET ");
  if (pos != std::string::npos)
  {
    pos = rcv_msg.find("/data?key=");
    if (pos != std::string::npos)
    {
      // extracting key
      pos = rcv_msg.find("=");
      std::string key = rcv_msg.substr(pos + 1);

      // if key exists in map return it
      if (data.count(key))
      {
        std::string msg = data[key];
        sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
      }
      // if key doesn't exit and node is root - reply with no data
      else if (root_flag)
      {
        std::string msg = "204 No data under specified key";
        sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
      }
      // no value for key - forward request to parent and wait for reply
      else
      {
        std::string msg = rcv_msg;
        sendto(parent_socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&parent_sockaddr, parentSockaddrLen);
        memset(&reply, 0, sizeof(reply));
        recvfrom(parent_socketfd, reply, 1024, 0, (struct sockaddr *)&parent_sockaddr, &parentSockaddrLen);
        msg = std::string(reply);
        if (msg != "204 No data under specified key")
        {
          data[key] = msg;
        }
        sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
      }
      return;
    }
  }
  // PUT message from node/client
  pos = rcv_msg.find("PUT ");
  if (pos != std::string::npos)
  {
    // parsing key and value from message
    pos = rcv_msg.find("/data?key=");
    if (pos != std::string::npos)
    {
      pos = rcv_msg.find("=");
      auto pos2 = rcv_msg.find("&");
      if (pos2 != std::string::npos)
      {
        std::string key = rcv_msg.substr(pos + 1, pos2 - (pos + 1));
        std::string partial_msg = rcv_msg.substr(pos2 + 1);
        pos = partial_msg.find("value=");
        if (pos != std::string::npos)
        {
          pos = partial_msg.find("=");
          std::string value = partial_msg.substr(pos + 1);
          // create record in map
          data[key] = value;
          std::string msg = "200 Success";
          // if not root - forward request to parent
          if (!root_flag)
          {
            std::string msg = rcv_msg;
            sendto(parent_socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&parent_sockaddr, parentSockaddrLen);
          }
          // reply to client
          sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
          return;
        }
      }
    }
  }
  // DELETE message from node/client
  pos = rcv_msg.find("DELETE ");
  if (pos != std::string::npos)
  {

    pos = rcv_msg.find("/data?key=");
    if (pos != std::string::npos)
    {
      // parsing key from message
      pos = rcv_msg.find("=");
      std::string key = rcv_msg.substr(pos + 1);
      std::string msg = "200 Success";

      // if key is in the map - remove
      if (data.erase(key) > 0)
      {
        msg = "200 Success";

        // if not root - forward request
        if (!root_flag)
        {
          std::string msg = rcv_msg;
          std::cout << "Forwarding: " << msg << std::endl;
          sendto(parent_socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&parent_sockaddr, parentSockaddrLen);
        }
      }
      // key isn't in map
      else
      {
        msg = "202 Key not in cache";
      }
      // reply to client
      sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
      return;
    }
  }

  // Request not recognized
  std::string msg = "400 Bad Request";
  sendto(server_socketfd, msg.c_str(), msg.length(), 0, (struct sockaddr *)&receiveSockaddr, receiveSockaddrLen);
  return;
}

int main()
{
  // get env variables
  char *env_root_addr;
  env_root_addr = getenv("ROOT_ADDR");

  char *env_node_addr;
  env_node_addr = getenv("NODE_ADDR");

  char *env_tree_depth;
  env_tree_depth = getenv("TREE_DEPTH");

  // check if all vars received
  if (env_root_addr == NULL)
  {
    std::cout << "Couldn't retrieve root address" << std::endl;
    return 0;
  }

  if (env_node_addr == NULL)
  {
    std::cout << "Couldn't retrieve node address" << std::endl;
    return 0;
  }

  if (env_tree_depth == NULL)
  {
    std::cout << "Couldn't retrieve tree depth" << std::endl;
    return 0;
  }

  // set env variables to global variables
  tree_depth = atoi(env_tree_depth);
  root_addr = std::string(env_root_addr);
  node_addr = std::string(env_node_addr);
  parent_addr = root_addr;

  // set root flag
  if (strcmp("ROOT", env_root_addr) == 0)
  {
    root_flag = true;
    std::cout << "I'm root" << std::endl;
  }

  if (root_flag)
  {
    // create root node at zookeeper
    register_root(node_addr);
  }
  else
  {
    // send GET_PARENT to root node to get parent node address
    std::string msg = "GET_PARENT " + node_addr;

    // try 5 times
    int try_count = 5;
    while (try_count > 0)
    {
      usleep(5000000);

      std::cout << node_addr << " Looking for parent" << std::endl;

      // setup and create socket
      int client_socketfd;
      socklen_t len = sizeof(sockaddr_in);
      auto iaddr = inet_addr(env_root_addr);
      struct sockaddr_in server_addr;
      if ((client_socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      {
        std::cout << "Socket error: " << strerror(errno) << "  " << errno << std::endl;
        try_count--;
        continue;
      }

      // root address
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = htons(PORT);
      server_addr.sin_addr.s_addr = iaddr;

      // socket timeout
      struct timeval tv;
      tv.tv_sec = 30;
      tv.tv_usec = 0;
      setsockopt(client_socketfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

      // send GET_PARENT message
      sendto(client_socketfd, msg.c_str(), msg.length(), 0, (sockaddr *)&server_addr, len);
      memset(&reply, 0, sizeof(reply));

      // wait for reply
      size_t result = recvfrom(client_socketfd, reply, 1024, 0, (struct sockaddr *)&server_addr, &len);
      if (result > 0)
      {
        std::cout << "Received reply: " << reply << std::endl;
        std::string str = std::string(reply);
        auto pos = str.find("PARENT ");
        if (pos != std::string::npos)
        {
          // set new parent address and break loop
          pos = str.find(" ");
          parent_addr = str.substr(pos + 1);
          break;
        }
      }

      // try again
      try_count--;
    }

    if (try_count == 0)
    {
      std::cout << "Couldn't get parent address - exiting" << std::endl;
      return 0;
    }
  }

  // Setup parent socket and address
  auto iaddr = inet_addr(parent_addr.c_str());

  if ((parent_socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    std::cout << "Socket error: " << strerror(errno) << "  " << errno << std::endl;
  }

  parent_sockaddr.sin_family = AF_INET;
  parent_sockaddr.sin_port = htons(PORT);
  parent_sockaddr.sin_addr.s_addr = iaddr;

  // listening socket creation and bind
  struct sockaddr_in sockaddr;
  server_socketfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(PORT);
  sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  int status = bind(server_socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (status == -1)
  {
    // couldn't bind, exit process
    std::cout << "Bind error: " << strerror(errno) << "  " << errno << std::endl;
    close(server_socketfd);
    return 0;
  }

  // listen for requests from clients/children
  while (true)
  {
    memset(&reply, 0, sizeof(reply));

    ssize_t result = recvfrom(server_socketfd, reply, 1024, 0, (struct sockaddr *)&receiveSockaddr, &receiveSockaddrLen);

    if (result > 0)
    {
      std::string str = reply;
      std::cout << "Received msg: " << str << std::endl;
      process_message(str);
    }
  }
}
