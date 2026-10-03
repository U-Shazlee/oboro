# Build environment for Oboro: devkitARM with the 3DS libraries, plus the two
# libraries built from source (OpenSSL, libexpat) and the CIA tools.
#
#   docker build -t oboro-build .
#   docker run --rm -v "$PWD":/oboro -w /oboro oboro-build make
#   docker run --rm -v "$PWD":/oboro -w /oboro oboro-build make cia
FROM devkitpro/devkitarm:latest

SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential autoconf automake libtool perl git wget unzip ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Libraries from devkitPro's own package manager (most are already in the image).
RUN dkp-pacman -Sy --needed --noconfirm 3ds-dev 3ds-curl 3ds-mbedtls 3ds-jansson 3ds-libopus 3ds-zlib

# CIA packaging tools.
RUN wget -q https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip && \
    unzip -q bannertool.zip -d /bannertool && \
    cp /bannertool/linux-x86_64/bannertool /usr/local/bin/ && chmod +x /usr/local/bin/bannertool && \
    rm -r /bannertool bannertool.zip
RUN wget -q https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.3/makerom-v0.18.3-ubuntu_x86_64.zip && \
    unzip -q makerom-v0.18.3-ubuntu_x86_64.zip -d /usr/local/bin && chmod +x /usr/local/bin/makerom && \
    rm makerom-v0.18.3-ubuntu_x86_64.zip

# OpenSSL and libexpat for the 3DS, installed into portlibs.
COPY tools/fetch-deps.sh tools/build-deps.sh /deps/tools/
RUN mkdir -p /deps/third_party && \
    source /etc/profile.d/devkit-env.sh && \
    /deps/tools/fetch-deps.sh && /deps/tools/build-deps.sh && \
    rm -rf /deps/third_party/openssl /deps/third_party/libexpat

CMD ["/bin/bash"]
