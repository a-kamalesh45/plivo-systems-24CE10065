#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "protocol.h"

#define MAX_PACKET 2048
#define NACK_COOLDOWN_S 0.030

int main() {
    // 1. Socket Initialization
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr));

    int player_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player_addr = {0};
    player_addr.sin_family = AF_INET;
    player_addr.sin_port = htons(47020);
    player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int feedback_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in feedback_addr = {0};
    feedback_addr.sin_family = AF_INET;
    feedback_addr.sin_port = htons(47003);
    feedback_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // 2. State Management
    double t0 = std::stod(std::getenv("T0"));
    double delay_ms = std::stod(std::getenv("DELAY_MS"));
    
    std::map<uint32_t, std::vector<uint8_t>> jitter_buffer;
    std::unordered_map<uint32_t, std::vector<uint8_t>> fec_buffer;
    std::unordered_map<uint32_t, double> last_nack_time;
    
    uint32_t next_play_seq = 0;
    uint32_t highest_seen = 0;
    double last_ack_time = 0;
    unsigned char buf[MAX_PACKET];

    struct pollfd fds[1];
    fds[0].fd = in_fd;
    fds[0].events = POLLIN;

    for (;;) {
        double now = current_time_s();
        double play_deadline = t0 + (delay_ms / 1000.0) + (next_play_seq * 0.020);
        int timeout_ms = std::max(0, static_cast<int>((play_deadline - now) * 1000.0));

        // A. Network Poll
        int ret = poll(fds, 1, timeout_ms);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            for (;;) {
                ssize_t n = recvfrom(in_fd, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr);
                if (n < 0) break;

                if (n >= static_cast<ssize_t>(sizeof(TransportHeader))) {
                    TransportHeader* hdr = reinterpret_cast<TransportHeader*>(buf);
                    uint32_t seq = ntohl(hdr->seq);
                    
                    if (hdr->type == PktType::MEDIA) {
                        highest_seen = std::max(highest_seen, seq);
                        std::vector<uint8_t> payload(buf + sizeof(TransportHeader), buf + n);
                        jitter_buffer[seq] = payload;
                    } 
                    else if (hdr->type == PktType::FEC) {
                        uint32_t block_base = ntohl(hdr->block_base);
                        std::vector<uint8_t> fec_payload(buf + sizeof(TransportHeader), buf + n);
                        fec_buffer[block_base] = fec_payload;
                    }

                    // FEC Reconstruction Pass
                    uint32_t base = (hdr->type == PktType::MEDIA) ? (seq % 2 == 0 ? seq : seq - 1) : ntohl(hdr->block_base);
                    
                    if (fec_buffer.count(base)) {
                        const auto& fec = fec_buffer[base];
                        bool has_0 = jitter_buffer.count(base);
                        bool has_1 = jitter_buffer.count(base + 1);

                        if (has_0 && !has_1) { 
                            std::vector<uint8_t> rec(fec.size());
                            for (size_t i = 0; i < fec.size(); i++) rec[i] = jitter_buffer[base][i] ^ fec[i];
                            jitter_buffer[base + 1] = rec;
                            highest_seen = std::max(highest_seen, base + 1);
                        } 
                        else if (!has_0 && has_1) {
                            std::vector<uint8_t> rec(fec.size());
                            for (size_t i = 0; i < fec.size(); i++) rec[i] = jitter_buffer[base + 1][i] ^ fec[i];
                            jitter_buffer[base] = rec;
                            highest_seen = std::max(highest_seen, base);
                        }
                    }

                    // Rate-Limited NACK Generation
                    now = current_time_s();
                    for (uint32_t i = next_play_seq; i < highest_seen; i++) {
                        if (!jitter_buffer.count(i)) {
                            if (now - last_nack_time[i] > NACK_COOLDOWN_S) {
                                TransportHeader nack;
                                nack.type = PktType::NACK;
                                nack.seq = 0;
                                nack.block_base = htonl(i); 
                                sendto(feedback_fd, &nack, sizeof(nack), 0, (struct sockaddr *)&feedback_addr, sizeof(feedback_addr));
                                last_nack_time[i] = now;
                            }
                        }
                    }

                    // Telemetry ACK Generation
                    if (now - last_ack_time >= 0.100) {
                        TransportHeader ack;
                        ack.type = PktType::ACK;
                        ack.seq = htonl(next_play_seq); 
                        ack.block_base = 0;
                        ack.timestamp = hdr->timestamp; 
                        sendto(feedback_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&feedback_addr, sizeof(feedback_addr));
                        last_ack_time = now;
                    }
                }
            }
        }

        // B. Playout Execution
        
        // 1. Flush all available contiguous packets immediately
        while (jitter_buffer.count(next_play_seq)) {
            const auto& pkt = jitter_buffer[next_play_seq];
            sendto(player_fd, pkt.data(), pkt.size(), 0, (struct sockaddr *)&player_addr, sizeof(player_addr));
            next_play_seq++;
        }

        // 2. Deadline clock for skipping unrecoverable gaps
        now = current_time_s();
        play_deadline = t0 + (delay_ms / 1000.0) + (next_play_seq * 0.020);
        if (now >= play_deadline - 0.002) {
            next_play_seq++; 
        }

        // 3. Garbage Collection (With 10-Packet Safety Watermark)
        uint32_t gc_watermark = (next_play_seq > 10) ? (next_play_seq - 10) : 0;
        
        auto j_it = jitter_buffer.begin();
        while (j_it != jitter_buffer.end()) {
            if (j_it->first < gc_watermark) j_it = jitter_buffer.erase(j_it);
            else ++j_it;
        }
        
        auto f_it = fec_buffer.begin();
        while (f_it != fec_buffer.end()) {
            if (f_it->first < gc_watermark) f_it = fec_buffer.erase(f_it);
            else ++f_it;
        }
        
        auto n_it = last_nack_time.begin();
        while (n_it != last_nack_time.end()) {
            if (n_it->first < gc_watermark) n_it = last_nack_time.erase(n_it);
            else ++n_it;
        }
    }
    return 0;
}