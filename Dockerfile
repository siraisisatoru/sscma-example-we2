FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    make \
    git \
    python3 \
    python3-pip \
    curl \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Install official ARM GNU Toolchain 13.2.Rel1 (x86_64 build)
# Uses bundled newlib which correctly defines __int64_t_defined — no toolchain_gnu.mk patches needed
# curl -fL: fail loudly on HTTP errors (-f) and follow redirects (-L)
RUN curl -fL -o /tmp/arm-toolchain.tar.xz \
    https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz \
    && tar -xf /tmp/arm-toolchain.tar.xz -C /opt \
    && rm /tmp/arm-toolchain.tar.xz

ENV PATH="/opt/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH"

RUN arm-none-eabi-gcc --version

# Python packages for xmodem flashing
RUN pip3 install --break-system-packages xmodem==0.4.7 pyserial==3.5

WORKDIR /workspace

CMD ["/bin/bash"]
