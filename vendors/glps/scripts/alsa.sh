#!/bin/bash

ALSA_GIT=https://github.com/alsa-project/alsa-lib.git

if [ -f /usr/include/alsa/asoundlib.h ] || dpkg -l | grep -q "libasound2"; then
  echo 'ALSA is installed'
else
  echo 'ALSA is not installed, installing...'
  
  sudo apt-get update
  sudo apt-get install -y build-essential git autoconf automake libtool
  
  # Clone and build alsa-lib
  git clone "$ALSA_GIT" alsa
  cd alsa || exit 1
  
  autoreconf -i
  ./configure
  make
  
  sudo make install
  
  cd ..
  rm -rf alsa
  
  sudo ldconfig
  
  echo 'ALSA has been installed from source'
fi