#===============================================================================
# file: doc/TOR.md
#===============================================================================
Add to torrc:
    HiddenServiceDir /var/lib/tor/llmserv
    HiddenServiceVersion 3
    HiddenServicePort 80 127.0.0.1:8080
Start llmserv bound to 127.0.0.1:8080. Use Tor Browser (JS disabled) to visit
the onion address. The UI contains only HTML/CSS (no JavaScript).

