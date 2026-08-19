cd .. && cmake -S . -B build && cd build && make && sudo make install && cd .. && cd examples && gcc x11_window.c -o test -lGLESv2 -lGLPS -L/usr/local/include  -lm && chmod +x test && ./test
