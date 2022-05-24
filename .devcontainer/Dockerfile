ARG ARCH=armv7hf
ARG VERSION=1.2
ARG UBUNTU_VERSION=20.04
ARG REPO=axisecp
ARG SDK=acap-native-sdk

FROM ${REPO}/${SDK}:${VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION}
#FROM axisecp/acap-sdk:3.2-armv7hf-ubuntu20.04
CMD /bin/bash

ENV DEBIAN_FRONTEND=noninteractive
COPY sources.list /etc/apt

RUN export ARCHDIR=arm-linux-gnueabihf && \
    export STRIP=arm-linux-gnueabihf-strip && \
    apt-get update && \
    dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -y cmake

ENV PKG_CONFIG_LIBDIR=/usr/share/pkgconfig:/usr/lib/$ARCHDIR/pkgconfig:/usr/lib/$ARCHDIR/pkgconfig

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
#RUN apt-get update && \
#    apt-get install mlocate

#WORKDIR /opt/app
RUN . /opt/axis/acapsdk/environment-setup*