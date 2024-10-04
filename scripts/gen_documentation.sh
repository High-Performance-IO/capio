#!/bin/bash

cd doxy

wget -O source.zip https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.3.4.zip source.zip
unzip source.zip
rm source.zip

doxygen