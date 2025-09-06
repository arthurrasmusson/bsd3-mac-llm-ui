# BSD3 MAC LLM UI

A tiny, auditable, **BSD 3‑Clause licensed** LLM chat UI designed around **Mandatory Access Control (MAC)** and **suckless** coding principles. It offers a **no‑JavaScript** web interface and optional **GTK/Qt** local GUI, routing prompts to either an **OpenAI‑compatible API** or a **local TensorRT‑LLM** backend (when compiled in).

Repository: [https://github.com/arthurrasmusson/bsd3-mac-llm-ui](https://github.com/arthurrasmusson/bsd3-mac-llm-ui)

---

## Who this is for

* People who need **strong isolation** between GUI and inference components (e.g., OpenXT, Qubes OS).
* Teams that prefer **small, readable C/C++** over large frameworks.
* Operators who want a **no‑JS** browser UI (works in Tor Browser with JS disabled).
* Security‑conscious deployments on **OpenBSD** or **Linux** with simple, explicit configuration.
* Users who need to run **offline/air‑gapped** or **VPN‑only** (WireGuard) environments.

## Who this is not for

* Folks who want a feature‑rich, highly interactive SPA; we intentionally **avoid JavaScript**.
* Large monoliths or plug‑in ecosystems.
* Situations where “convenience over control” is preferred.
* Anyone needing streaming tokens to render mid‑generation updates (no‑JS means **full response on page reload**).

---

## What it does (in one paragraph)

**BSD3 MAC LLM UI** is a small HTTP server (C) that serves a static HTML/CSS chat form (no JS), accepts prompts, and forwards them to either an **OpenAI‑compatible** endpoint (HTTPS) or, when compiled with the optional C++ shim, to a **local TensorRT‑LLM** runtime—keeping the LLM entirely in your compartment/VM. It also supports an optional **`--local-gui`** mode to run a minimal **GTK** or **Qt** desktop UI instead of serving HTTP.

---

## Design goals

* **Minimal attack surface**: no JS, no template engines, no dynamic plug‑ins.
* **Small codebase**: straightforward C and a thin C++ layer for the optional TRT‑LLM backend.
* **MAC‑friendly**: designed to slot into **compartmentalized** and **policy‑enforced** topologies (OpenXT/Qubes).
* **Transport flexibility**:

  * Direct HTTPS to OpenAI‑compatible servers, **or**
  * **No‑network** operation with in‑process TensorRT‑LLM (C++ bindings), **or**
  * Hypervisor‑mediated IPC (HMX; e.g., qrexec) between GUI VM and LLM VM.

---

## Security technologies leveraged

* **OpenBSD**: `pledge(2)` (and structure compatible with `unveil(2)` patterns if you customize file access).
* **Linux**: optional **seccomp** filter to hard‑block `connect(2)` when running `--no-network`.
* **No JavaScript** UI: substantially reduces client‑side attack surface; safe to use with Tor Browser JS disabled.
* **Strict HTTP**: short timeouts, small fixed caps on request/response sizes, conservative parsing.
* **Security headers**: `Content-Security-Policy` (default‑deny), `X-Frame-Options: DENY`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store`.
* **Stateless by default**: transcript is carried in a hidden `<textarea>`; no server‑side sessions or database.
* **Compartment‑first**: can route through **HMX** (e.g., `qrexec-client-vm`) so GUI VM never holds network capability.

> **Note on TLS:** the libtls path in the reference implementation disables certificate verification by default (to keep the code tiny). For production, either (1) enable proper CA verification in the code, or (2) use the `curl(1)` fallback with your system CA trust store. See “TLS choices” below.

---

## Architecture (at a glance)

```
[ Browser (no JS) ]  ──HTTP POST──>  [ llmserv (C) ]
                                        |   ^
                                        |   |
                            [llm_backend.h (C ABI)]
                              /                \
                   [OpenAI-compatible]    [TensorRT‑LLM]
                        HTTPS (libtls           C++ in‑proc
                       or curl fallback)     (optional, offline)

Alternative transport:
[ GUI/Web VM ] -- HMX (qrexec/stdio JSON) --> [ LLM VM (OpenXT/Qubes) ]
```

---

## Supported workflows / topologies

1. **Local web (localhost only, offline inference)**

   * Build with TRT‑LLM backend; run with `--no-network`.
   * Serve only on `127.0.0.1`; browse locally.
   * GUI VM and inference code can be the same VM or separate via HMX.

2. **Local desktop GUI (no web)**

   * `--local-gui gtk` or `--local-gui qt`.
   * Uses the same backends as the web mode (OpenAI or TRT‑LLM).

3. **Tor hidden service**

   * Bind the server to `127.0.0.1:PORT`; publish via Tor hidden service mapping `onion:80 -> 127.0.0.1:PORT`.
   * Works with JS disabled.

4. **WireGuard**

   * Bind to the WG interface IP (e.g., `10.66.66.2:8080`) and allow only VPN peers.

5. **Split‑VM via HMX (OpenXT/Qubes)**

   * GUI/Web VM runs `llmserv` with `--no-network --hmx-command ...` to a mediator in an LLM VM.
   * The mediator converts stdin/stdout JSON to the local TRT‑LLM bindings or to an OpenAI‑compatible server reachable only from the LLM VM.

---

## Build & install

> The project follows **suckless** conventions: compile‑time defaults live in `config.h` (generated from `config.def.h` on first build); toggles are **Makefile** variables.

### Prerequisites

* **OpenBSD**: base toolchain; `libtls` is part of LibreSSL.
* **Linux**:

  * For TLS via libtls: `libretls-dev` (or distro equivalent), or rely on the **curl fallback**.
  * For seccomp (optional): `libseccomp-dev` (if link fails, disable with `WITH_SECCOMP=0`).
  * `curl(1)` present if you use the fallback.

### Quick builds

```sh
# Default: web server + OpenAI backend, TLS via libtls/libretls if available
make

# Explicit TLS via libtls/libretls (Linux example)
make clean
make TLS_BACKEND=libtls

# Disable Linux seccomp if your system lacks libseccomp
make clean
make WITH_SECCOMP=0

# Add GTK or Qt local GUI
make clean
make WITH_GTK=1
# or
make WITH_QT=1

# Install (OpenBSD: doas, Linux: sudo)
make install
```

> **No TensorRT‑LLM on this machine?** You don’t need it. The TRT backend is optional. If you add it later, build with:
> `make HAVE_TRTLLM=1 TRTLLM_CXXFLAGS="..." TRTLLM_LIBS="..."`

---

## TLS choices

* **libtls/libretls path**: minimal, auditable; in the reference build we set `insecure_noverifycert()` for simplicity. For production, **enable verification** (trust anchors/pin) in the code before deployment.
* **curl fallback**: uses your system’s CA store and a small `execvp("curl", argv)` without a shell.

---

## Configuration

### Compile‑time (`config.h`)

* Default bind address, default backend (`openai` or `trtllm`), model, temperature, caps (body sizes, timeouts), CSS/theme, and headers.
* Copy `config.def.h` to `config.h` and edit, or just rely on runtime flags.

### Runtime flags (most common)

```text
--bind HOST:PORT           # default: 127.0.0.1:8080
--backend openai|trtllm    # default: openai (compile-time)
--api-base URL             # OpenAI-compatible base (default https://api.openai.com)
--api-key-file FILE        # alternatively set OPENAI_API_KEY
--model NAME               # model id/name
--temp FLOAT               # temperature (0..2)
--max-tokens N
--no-network               # disallow outbound connect(); (Linux seccomp kills connect)
--hmx-command CMD ... --   # use HMX (e.g., qrexec) instead of networking
--trtllm-engine PATH       # TRT engine (when compiled with TRT backend)
--local-gui gtk|qt         # desktop UI instead of web
-v                         # verbose logs to stderr
```

Environment:

* `OPENAI_API_KEY` — used if `--api-key-file` is not provided.

---

## Usage examples

### 1) OpenAI‑compatible (localhost)

```sh
export OPENAI_API_KEY=sk-...
./llmserv --bind 127.0.0.1:8080 --backend openai
# open http://127.0.0.1:8080 in your browser (JS can be disabled)
```

### 2) Over WireGuard

```sh
./llmserv --bind 10.66.66.2:8080 --backend openai
# allow only WG peers to connect
```

### 3) Tor Hidden Service

Tor `torrc`:

```text
HiddenServiceDir /var/lib/tor/llmserv
HiddenServiceVersion 3
HiddenServicePort 80 127.0.0.1:8080
```

Run:

```sh
./llmserv --bind 127.0.0.1:8080 --backend openai
```

### 4) Split‑VM via HMX (OpenXT/Qubes)

GUI/Web VM:

```sh
./llmserv --bind 127.0.0.1:8080 --no-network \
  --hmx-command qrexec-client-vm llm-vm my.org.LLM.Chat --
```

LLM VM mediator (conceptual):

* Reads a **/v1/chat/completions‑shaped** JSON from stdin.
* Forwards to **TRT‑LLM** locally (no network) or to an OpenAI‑compatible endpoint accessible **only** from the LLM VM.
* Writes the JSON response to stdout.

### 5) Local GUI instead of web

GTK:

```sh
./llmserv --local-gui gtk --backend openai
```

Qt:

```sh
./llmserv --local-gui qt --backend openai
```

### 6) Offline/air‑gapped (TRT‑LLM)

```sh
./llmserv --bind 127.0.0.1:8080 \
  --backend trtllm --no-network \
  --trtllm-engine /path/to/engine
```

---

## Threat‑model notes & limitations

* **No JS** significantly reduces client‑side risks but also means **no streaming** output and page reload after each prompt.
* **Stateless** by default: transcript lives in a hidden form field. This makes reverse proxies and split VMs easy but caps history length by design.
* **OpenBSD**: we can’t both `listen()` and absolutely prevent `connect()` via `pledge()` granularity; in `--no-network` mode the code path avoids networking, but for strong isolation prefer **HMX** split.
* **TLS verification** is **not** enabled in the minimal libtls path; turn it on or use curl/system trust for production.
* **Small caps** are deliberate (request/response caps, timeouts). Adjust in `config.h` if needed.

---

## Roadmap

* **FLASK/XSM** integration and policy recipes for Xen‑based systems.
* First‑class **Hypervisor Mediated Exchange** (HMX) protocol module:

  * Reference mediators for **OpenXT** (and Qubes via qrexec).
  * Schema validation for stdin/stdout JSON.
* Hardened TLS config:

  * CA‑verified libtls path with pinning options.
  * Optional mTLS for internal links.
* Optional **state store** (opt‑in): file‑backed transcripts with obvious, auditable format.
* Token streaming via **multipart** or **server‑side flushing** (still no JS).
* Packaged examples for **air‑gapped** TRT‑LLM deployments (engine loading, tokenizer stubs).

---

## Contributing

* Keep changes **small and auditable**.
* Prefer compile‑time switches over runtime magic.
* Match the code style (headers, `config.h`, `arg`‑style parsing).
* Discuss MAC/compartment designs in issues before large changes.
* PRs that improve **TLS verification**, **HMX** integration, or **policy examples** (FLASK/XSM) are very welcome.

---

## License

**BSD 3‑Clause License (BSD‑3‑Clause)** — see `LICENSE` in the repository.

---

## Credits & acknowledgments

* Heavily inspired by the **suckless** philosophy: minimalism, clarity, and control.
* Thanks to the OpenBSD and Linux security communities for the strong primitives we build on.
* TensorRT‑LLM is an optional dependency; this project is not affiliated with NVIDIA.

---

## FAQ (quick)

* **Q: Can I point this at any OpenAI‑compatible server?**
  **A:** Yes. Use `--api-base` and `OPENAI_API_KEY`/`--api-key-file`.

* **Q: Does it store my prompts or responses?**
  **A:** Not by default. History is carried in a hidden form field; server logs go to stderr. You can redirect or disable as you wish.

* **Q: How do I prevent any network egress from the GUI/Web VM?**
  **A:** Run with `--no-network` and route through `--hmx-command` to an LLM VM. On Linux, `connect(2)` is blocked via seccomp; on OpenBSD, rely on split domains and auditing.

---

If you have a specific deployment target (OpenBSD jail, systemd unit, OpenXT domain policy, Tor only, or WireGuard‑only) open an issue in the repo and we’ll add a minimal example.
