# Remote Benchmarking (client and server on separate machines)

Run the benchmark client on a **different machine** than the server. This is the
setup you want for trustworthy numbers: co-locating client and server on one host
makes them fight for cores, and the client's scheduling jitter leaks into the
server's measured tail latency (see [benchmarking.md](benchmarking.md) for the
coordinated-omission background).

```
  ┌─────────────────────┐         LAN          ┌─────────────────────┐
  │  CLIENT machine      │  ───────────────▶    │  SERVER machine      │
  │  fkvs-benchmark      │     TCP :5995        │  fkvs-server         │
  │  scripts/bench-sweep │  ◀───────────────    │  (single-threaded)   │
  └─────────────────────┘     responses        └─────────────────────┘
```

> ⚠️ fkvs has **no authentication**. Only expose it on a trusted private LAN,
> never the public internet.

---

## 1. Server machine

By default the server binds to loopback only, so a remote client cannot reach it.
Three things to set up.

### 1.1 Bind to the network

Edit `server.conf` and change the bind address from `127.0.0.1` to the server's
LAN IP (preferred over `0.0.0.0` because it limits exposure to one interface):

```conf
# server.conf
bind 192.168.100.11      # ← this machine's LAN IP
port 5995
max-clients 512          # see 1.3
```

Find the LAN IP with:

```bash
# macOS
ipconfig getifaddr en0 || ipconfig getifaddr en1
# Linux
hostname -I | awk '{print $1}'
```

The bind only takes effect on restart, so restart the server:

```bash
./fkvs-server -c server.conf
```

Confirm it's now listening on the LAN address (not `127.0.0.1`):

```bash
# macOS
lsof -nP -iTCP:5995 -sTCP:LISTEN
# Linux
ss -ltnp 'sport = :5995'
```

### 1.2 Open the firewall for port 5995

**macOS** (application firewall) — allow the server binary:

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add    "$(pwd)/fkvs-server"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp "$(pwd)/fkvs-server"
```

**Linux** (if a firewall is active):

```bash
sudo ufw allow 5995/tcp                       # ufw
# or
sudo firewall-cmd --add-port=5995/tcp         # firewalld
```

### 1.3 Size `max-clients` for the load

`max-clients` caps concurrent connections. The benchmark's `-c` (clients) must
stay under it. Chasing high request rates over a network needs **many**
connections, so set this generously (e.g. `512`) and restart.

### 1.4 Keep the server host quiet

The server is single-threaded — background load steals its core and shows up as
latency. Close heavy apps. On Linux you can pin it to an isolated performance
core:

```bash
taskset -c 2 ./fkvs-server -c server.conf      # Linux only
```

---

## 2. Client machine

### 2.1 Get the tools onto the client

The benchmark binary is **not portable across OS/arch**. Pick one:

- **Same OS + CPU arch** as where it was built (e.g. both Apple Silicon macOS):
  copy the binary and the script.
  ```bash
  scp fkvs-benchmark scripts/bench-sweep.sh user@CLIENT:~/
  ```
- **Different OS/arch** (e.g. Linux client): clone the repo on the client and
  build it there.
  ```bash
  git clone <repo-url> fkvs && cd fkvs
  make -f Makefile.fkvs setup-and-build
  ```

### 2.2 Smoke-test connectivity first

From the client, with `SERVER_IP` set to the server's LAN IP:

```bash
SERVER_IP=192.168.100.11

nc -vz "$SERVER_IP" 5995                                   # port reachable?
./fkvs-benchmark -h "$SERVER_IP" -p 5995 -c 1 -n 1000 -t ping   # expect Failed: 0
```

If `nc` hangs or the benchmark reports failures, revisit the bind address (1.1)
and firewall (1.2).

### 2.3 Run the throughput-at-p99 sweep

```bash
# If the binary sits next to the script, -b is optional.
./bench-sweep.sh -H "$SERVER_IP" -c 64 -d 10 -s 1 \
    -b ./fkvs-benchmark \
    100k 250k 500k 1m 2m 4m
```

This sweeps each target rate, records coordinated-omission-corrected latency, and
prints the **knee** — the highest rate sustained within the p99 SLO (`-s`, in ms).
See [benchmarking.md](benchmarking.md) for how to read the table.

---

## 3. Two ceilings to check before trusting a "4M req/s" result

1. **Network packets-per-second, not bandwidth.** The open-model `-R` mode is
   depth-1: **one packet per request, each direction**. 4M req/s ≈ 4M pps in +
   4M pps out. A 1GbE link tops out near ~1–1.5M pps for small frames, so
   **1GbE caps you around ~1M req/s no matter how fast the server is** — you need
   10GbE+ to even attempt 4M over the wire. Byte throughput is trivial (tens of
   MB/s); pps is the wall. Check the link speed:
   ```bash
   networksetup -getmedia en0           # macOS
   ethtool eth0 | grep Speed            # Linux
   ```

2. **`-R` measures the latency knee, not peak throughput.** Because it's depth-1,
   reaching multi-million req/s also needs many connections (high `-c`) and may
   need a **second client machine** so the client isn't the bottleneck. For a raw
   peak-throughput number, use the closed-loop pipelined mode (`-P`) instead; use
   `-R` to report capacity honestly as "X req/s at p99 < Y ms".

---

## 4. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `nc` connection refused | server bound to `127.0.0.1` | set `bind <LAN-IP>` in `server.conf`, restart (1.1) |
| `nc` times out / hangs | firewall blocking 5995 | allow the port (1.2) |
| benchmark `Failed > 0` at low rate | server saturated or wrong host | check `-h`, server load, `max-clients` (1.3) |
| throughput plateaus far below target | network pps/link ceiling | check link speed; need 10GbE for multi-M (§3.1) |
| achieved ≈ target but p99 huge | server past its knee | that's the honest signal — lower the rate (§2.3) |
| `address already in use` on restart | old server still bound | stop the previous `fkvs-server` first |
