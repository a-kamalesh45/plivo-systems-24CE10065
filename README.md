# Hybrid Reliable UDP Transport Protocol

This project implements a reliable UDP transport protocol for real-time media streaming. It uses XOR-based FEC, ACK/NACK feedback, retransmissions, and a receiver-side jitter buffer to reduce deadline misses while keeping bandwidth under the assignment limits.

## Build

```bash
make clean
make
```

## Run

```bash
python3 run.py --profile profiles/A.json --delay_ms 44
python3 run.py --profile profiles/B.json --delay_ms 96
```

See `NOTES.md`, `RUNLOG.md`, and `SUMMARY.html` for the design details and experimental results.
