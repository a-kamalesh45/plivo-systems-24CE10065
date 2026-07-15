# RUNLOG.md

# Experiment Log

The objective of the assignment was to design a transport protocol that minimizes playback deadline misses while keeping total bandwidth overhead below **2×**. The baseline implementation supplied with the handout simply forwarded packets once without any recovery mechanism, making it highly susceptible to packet loss and network jitter. The development process therefore focused on progressively improving reliability while preserving low latency.

---

## Experiment 1 – Baseline Forwarding

**Design**

- Direct packet forwarding.
- No redundancy.
- No retransmissions.
- No receiver buffering.

**Observation**

Any packet dropped by the relay was permanently lost. Even packets that eventually arrived after heavy network jitter frequently missed the playback deadline.

**Conclusion**

A recovery mechanism was necessary.

---

## Experiment 2 – XOR Forward Error Correction

**Change**

Implemented simple XOR parity.

Every two media packets generate one parity packet.

```
Media 0
Media 1
Parity
```

A single lost packet inside the pair can be reconstructed immediately without waiting for sender feedback.

**Reason**

Forward Error Correction recovers isolated losses without introducing an additional network round trip.

**Observation**

Deadline misses decreased significantly, but burst losses and parity failures still caused unrecoverable gaps.

---

## Experiment 3 – Jitter Buffer

**Change**

Added a receiver-side jitter buffer.

Packets are temporarily stored and delivered in sequence instead of immediately forwarding every arrival.

**Reason**

The relay introduces random delay and packet reordering. Without buffering, packets that arrived slightly late were unnecessarily marked as missed.

**Observation**

Packet ordering improved considerably and deadline misses caused purely by jitter were reduced.

---

## Experiment 4 – Selective Retransmission

**Change**

Introduced NACK packets.

Whenever the receiver detects a missing sequence number that cannot be recovered using parity, it sends a NACK requesting retransmission.

**Reason**

Although FEC recovers isolated losses, parity cannot recover every failure. Selective retransmission provides a second recovery mechanism while avoiding the cost of retransmitting every packet.

**Observation**

Miss rate improved further while keeping bandwidth below the assignment limit.

---

## Experiment 5 – ACK Support

**Change**

Added ACK packets.

The receiver periodically acknowledges successfully delivered packets.

The sender uses ACKs to remove obsolete packets from its transmission history.

**Reason**

Without acknowledgements, sender memory would continue growing during long executions.

**Observation**

Memory usage remained bounded and retransmission history became significantly cleaner.

---

## Experiment 6 – RTT Estimation

**Change**

Added timestamps to protocol packets.

The sender computes a Smoothed Round Trip Time (SRTT) using ACK timestamps.

```
SRTT =
0.875 × previous
+
0.125 × latest RTT
```

**Reason**

Although retransmissions remain receiver-driven, RTT estimation provides useful protocol telemetry and lays the foundation for adaptive timeout selection.

---

## Experiment 7 – Memory Management

**Change**

Implemented garbage collection on both sender and receiver.

Old packets, parity blocks and retransmission state are periodically removed once they are no longer useful.

**Reason**

Long-running executions should consume bounded memory.

**Observation**

Memory usage remained stable throughout the experiment.

---

# Parameter Tuning

After the protocol architecture stabilized, the remaining work focused on identifying the minimum playout delay that consistently satisfied the grading constraints.

---

## Profile A (profiles/A.json)

| delay_ms | Miss % | Overhead | Result |
|----------:|-------:|---------:|--------|
| 40 | 4.20–4.60% | 1.76× | FAIL |
| 41 | 1.67% | 1.76× | FAIL |
| 42 | 0.87% | 1.76× | PASS |
| 44 | 0.80% | 1.76× | PASS |
| 48 | 0.87% | 1.76× | PASS |
| 60 | 0.27% | 1.76× | PASS |
| 80 | 0.20% | 1.76× | PASS |

The transition between failure and success occurs between **41 ms** and **42 ms**. A grading value of **44 ms** was selected to provide additional safety margin against run-to-run randomness while maintaining a very low playback delay.

---

## Profile B (profiles/B.json)

| delay_ms | Miss % | Overhead | Result |
|----------:|-------:|---------:|--------|
| 80 | 2.80% | 1.92× | FAIL |
| 90 | 1.53–1.60% | 1.93–1.94× | FAIL |
| 92 | 1.07% | 1.92× | FAIL |
| 93 | 1.40% | 1.93× | FAIL |
| 94 | 0.87% | 1.92× | PASS |
| 96 | 0.93% | 1.93× | PASS |
| 100 | 0.93% | 1.94× | PASS |

The minimum passing delay was observed at **94 ms**. For the final submission, **96 ms** was chosen to improve robustness while remaining close to the minimum achievable latency.

---

# Final Design

The final protocol combines several complementary reliability mechanisms:

- Custom transport header.
- XOR Forward Error Correction.
- Receiver-side jitter buffering.
- Selective NACK-based retransmission.
- ACK-based sender cleanup.
- Smoothed RTT estimation.
- Bounded sender and receiver memory.
- Deadline-aware packet delivery.

Rather than relying on a single recovery technique, the protocol uses FEC to recover common isolated losses immediately and retransmissions only when parity recovery is insufficient. This combination achieves a lower playback delay than either approach alone while remaining within the assignment's bandwidth constraint.

---

# Final Results

## Profile A

- Recommended delay: **44 ms**
- Deadline misses: **0.80%**
- Bandwidth overhead: **1.76×**
- Result: **VALID**

## Profile B

- Recommended delay: **96 ms**
- Deadline misses: **0.93%**
- Bandwidth overhead: **1.93×**
- Result: **VALID**

The final implementation satisfies all grading requirements while operating close to the minimum achievable playback delay for both supplied network profiles.