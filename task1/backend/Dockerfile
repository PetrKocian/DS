#
# Simple backend demo server image
#

FROM ghcr.io/maxotta/kiv-ds-docker:latest

RUN yum -q -y install gcc-c++

COPY program /opt/backend

WORKDIR /opt/backend

#RUN clang++ -o Test main.cpp

#CMD ["./Test"]

# EOF
