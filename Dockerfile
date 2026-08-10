FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# ------------------------------------------------------------
# Base system packages
# ------------------------------------------------------------
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y \
        git \
        openssh-client \
        ssh \
        vim \
        gdb \
        wget \
        curl \
        ca-certificates \
        software-properties-common \
        build-essential \
        cmake \
        ninja-build \
        flex \
        bison \
        python3 \
        python3-pip \
        python3-venv \
        lsb-release \
        pkg-config \
        catch2 && \
    rm -rf /var/lib/apt/lists/*

# ------------------------------------------------------------
# Python environment
# ------------------------------------------------------------
RUN python3 -m venv /opt/venv

ENV PATH="/opt/venv/bin:${PATH}"

RUN pip install --upgrade pip && \
    pip install lit conan

# ------------------------------------------------------------
# LLVM 20 + MLIR
# ------------------------------------------------------------
WORKDIR /tmp

RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 20

RUN apt-get update && \
    apt-get install -y \
        clang-20 \
        llvm-20 \
        libclang-20-dev \
        llvm-20-dev \
        libmlir-20-dev \
        mlir-20-tools \
        clang-tidy-20 && \
    rm -rf /var/lib/apt/lists/*

# ------------------------------------------------------------
# Apache Arrow 24
# ------------------------------------------------------------
RUN wget https://apache.jfrog.io/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb && \
    apt-get install -y -V ./apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb

RUN apt-get update && \
    apt-get install -y \
        libarrow-dev=24.* \
        libarrow-compute-dev=24.* \
        libparquet-dev=24.* \
        libboost-context1.83-dev && \
    rm -rf /var/lib/apt/lists/*

# ------------------------------------------------------------
# Conan setup
# ------------------------------------------------------------
RUN conan profile detect --force && \
    conan remote add dice-group \
    https://conan.dice-research.org/artifactory/api/conan/tentris

# ------------------------------------------------------------
# Build rdf4cpp
# ------------------------------------------------------------
WORKDIR /opt

RUN git clone https://github.com/tentris/rdf4cpp.git && \
    cd rdf4cpp && \
    git checkout v0.1.13 && \
    wget https://github.com/conan-io/cmake-conan/raw/develop2/conan_provider.cmake \
        -O conan_provider.cmake && \
    cmake -B build_dir \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=conan_provider.cmake && \
    cmake --build build_dir -j$(nproc) && \
    cd build_dir && \
    make install

# ------------------------------------------------------------
# Java OpenJDK
# ------------------------------------------------------------
RUN apt-get update \
    && apt-get install -y --no-install-recommends openjdk-17-jdk \
    && rm -rf /var/lib/apt/lists/*

# ------------------------------------------------------------
# SSH setup
# ------------------------------------------------------------
RUN mkdir -p /root/.ssh && \
    ssh-keyscan github.com >> /root/.ssh/known_hosts

# ------------------------------------------------------------
# Workspace
# ------------------------------------------------------------

WORKDIR /workspace

# ------------------------------------------------------------
# Default shell
# ------------------------------------------------------------
CMD ["/bin/bash"]
