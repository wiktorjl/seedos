#!/bin/bash

make clean

make && ./build-iso.sh && ./run_efi.sh
