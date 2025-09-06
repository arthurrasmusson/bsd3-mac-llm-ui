#===============================================================================
# file: doc/OPENBSD.md
#===============================================================================
Build:
    doas pkg_add libressl
    make TLS_BACKEND=libtls
Run (OpenAI over network):
    OPENAI_API_KEY=sk-... ./llmserv --bind 127.0.0.1:8080 --backend openai
Run (local TRT-LLM, no network):
    ./llmserv --bind 127.0.0.1:8080 --backend trtllm --no-network --trtllm-engine /path/engine
Tor hidden service:
    HiddenServiceDir /var/tor/llmserv
    HiddenServiceVersion 3
    HiddenServicePort 80 127.0.0.1:8080

