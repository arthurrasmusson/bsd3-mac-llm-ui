#===============================================================================
# file: doc/LINUX.md
#===============================================================================
Deps (option A: libretls):
    apt-get install libretls-dev
Build:
    make TLS_BACKEND=libtls
Option B (no libtls): use curl(1) fallback by omitting TLS_BACKEND=libtls and
adapting backend_openai.c if needed.
Run with seccomp blocking outbound connect in TRT mode:
    ./llmserv --backend trtllm --no-network --bind 127.0.0.1:8080 --trtllm-engine /path/engine
WireGuard hosting:
    ./llmserv --bind 10.66.66.2:8080 ...

