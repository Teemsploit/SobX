#!/usr/bin/env bash

gcc -O2 -fPIC -shared injected_lib.c -o injected_lib.so -ldl -pthread

gcc -O2 Injector.c -o injector -ldl -pthread

gcc -O2 AtingleUI.c -o atingle_ui \
  `pkg-config --cflags --libs gtk4`
