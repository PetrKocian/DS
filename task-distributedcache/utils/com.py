import socket
import sys
import requests

argv = sys.argv

if len(argv) == 1:
    print("USAGE:\npython3 com.py <address> <operation> <key>")
    sys.exit()

UDP_IP = argv[1]
UDP_PORT = 1234

if len(argv) < 4:
    print("Not enough arguments\n\nUSAGE:\npython3 com.py <address> <operation> <key>")
    sys.exit()

if argv[2] == "DELETE":
    MESSAGE = argv[2] + " /data?key=" + argv[3]
elif argv[2] == "GET":
    MESSAGE = argv[2] + " /data?key=" + argv[3]
elif argv[2] == "PUT":
    if len(argv) < 5:
        print("Not enough arguments\n\nPUT USAGE:\npython3 com.py <address> PUT <key> <value>")
        sys.exit()
    MESSAGE = argv[2] + " /data?key=" + argv[3] + "&value=" + argv[4]
else:
    print("Valid operations: GET, PUT, DELETE")
    sys.exit()

print ("Sending: " + MESSAGE)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(bytes(MESSAGE, "utf-8"), (UDP_IP, UDP_PORT))

data, addr = sock.recvfrom(1024)
print ("Reply: " + str(data.decode('UTF-8')))
