FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    make \
    git \
    python3 \
    python3-pip \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    && rm -rf /var/lib/apt/lists/*

RUN arm-none-eabi-gcc --version

# Python packages for xmodem flashing
RUN pip3 install xmodem==0.4.7 pyserial==3.5

WORKDIR /workspace

CMD ["/bin/bash"]
