#
# Simple backend demo server image
#

FROM ghcr.io/maxotta/kiv-ds-docker:latest

RUN yum -q -y install gcc-c++

COPY program /opt/backend

WORKDIR /opt/backend

RUN g++ --std=c++11 main.cpp

CMD /opt/backend/./a.out

# EOF
