#
# Simple backend demo server image
#

FROM ghcr.io/maxotta/kiv-ds-docker:latest

RUN yum -q -y install python3-devel

RUN pip3 install kazoo

RUN yum -q -y install gcc-c++

COPY program /opt/backend
#COPY program /usr/lib64/python3.6

WORKDIR /opt/backend

RUN g++ -pthread --std=c++11 main.cpp -I/usr/include/python3.6m/ -lpython3.6m

CMD /opt/backend/./a.out

# EOF
