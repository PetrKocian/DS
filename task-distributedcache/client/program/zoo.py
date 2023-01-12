
import sys
import random
import os
#sys.path.append('/home/petrk/.local/lib/python2.7/site-packages')

# ZOOKEEPER
from kazoo.client import KazooClient

# recursive method to create node in tree
def get_parent_rec(zk : KazooClient, address, curr_depth, max_depth, node):
    children = zk.get_children(address)
    if len(children) < 2:
        zk.create(f'{address+"/"+node}', makepath=True)
        return address
    elif curr_depth < max_depth:
        for x in children:
            ret = get_parent_rec(zk, address + "/" + x, curr_depth+1, max_depth, node)
            if str(ret) != "None":
                return ret
    else:
        return "None"

# Get parent node which doesn't have 2 children yet
def get_parent(node_address, root_address, tree_depth):
    root_addr = "/" + root_address

    # connect to zookeeper
    zk = KazooClient(hosts='10.0.1.100:2181')
    zk.start()

    # get root children
    children = zk.get_children(root_addr)

    # get address of newly created node
    res = get_parent_rec(zk, root_addr, 2, tree_depth,str(node_address))

    if res == None:
        return "TREE_FULL"
    else:
        index = res.rfind("/")
        print(index)
        res_final = res[index+1:]
        return str(res_final)

    # extract parent address and return it


def register_root(address):
    print("Registering root at zookeper")
    
    # connect to zookeeper
    zk = KazooClient(hosts='10.0.1.100:2181')
    zk.start()
    
    # create root node, if it hasn't been created yet
    if zk.exists(f'{address}')==None:
        print("reg")
        zk.create(f'{address}', makepath=True)
 
    #stop zookeeper client
    zk.stop()
    return "success root registered"
