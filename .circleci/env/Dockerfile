FROM ubuntu:xenial-20180808

# Install system dependencies.
RUN set -e -x ;\
    apt-get -y update ;\
    apt-get -y upgrade ;\
    apt-get -y install build-essential qtbase5-dev cmake python3-dev \
        libboost-all-dev git pkg-config libftdi-dev lcov
