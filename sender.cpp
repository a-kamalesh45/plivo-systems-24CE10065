#include <iostream>
#include <vector>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include "protocol.h"

#define MAX_PACKET 2048
#define HARNESS_PAYLOAD_SIZE 164

int main() {
    // 1. Socket Initialization
    int source_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in source_addr = {0};
    source_addr.sin_family = AF_INET;
    source_addr.sin_port = htons(47010);
    source_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(source_fd, (struct sockaddr *)&source_addr, sizeof(source_addr));

    int relay_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay_addr = {0};
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(47001);
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int feedback_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in feedback_addr = {0};
    feedback_addr.sin_family = AF_INET;
    feedback_addr.sin_port = htons(47004);
    feedback_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(feedback_fd, (struct sockaddr *)&feedback_addr, sizeof(feedback_addr));

    struct pollfd fds[2];
    fds[0].fd = source_fd;   fds[0].events = POLLIN;
    fds[1].fd = feedback_fd; fds[1].events = POLLIN;

    // 2. State & Telemetry
    std::unordered_map<uint32_t, std::vector<uint8_t>> window;
    unsigned char buf[MAX_PACKET];
    double smoothed_rtt = 0.050; // Start with 50ms assumption

    // 3. Execution Loop
    for (;;) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) continue;

        // A. Drain Harness Source (Media generation)
        if (fds[0].revents & POLLIN) {
            for (;;) {
                ssize_t n = recvfrom(source_fd, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr);
                if (n < 0) break; 

                if (n == HARNESS_PAYLOAD_SIZE) {
                    uint32_t seq = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
                    window[seq] = std::vector<uint8_t>(buf, buf + n);

                    // Transmit Encapsulated Media Packet
                    std::vector<uint8_t> out_pkt(sizeof(TransportHeader) + n);
                    TransportHeader* hdr = reinterpret_cast<TransportHeader*>(out_pkt.data());
                    hdr->type = PktType::MEDIA;
                    hdr->seq = htonl(seq);
                    hdr->block_base = 0;
                    hdr->timestamp = current_time_s(); 
                    std::memcpy(out_pkt.data() + sizeof(TransportHeader), buf, n);
                    sendto(relay_fd, out_pkt.data(), out_pkt.size(), 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));

                    // Generate & Transmit FEC Parity Block (Every 2 frames)
                    if (seq % 2 == 1 && window.count(seq - 1)) {
                        const auto& prev = window[seq - 1];
                        std::vector<uint8_t> fec_pkt(sizeof(TransportHeader) + n);
                        
                        TransportHeader* fec_hdr = reinterpret_cast<TransportHeader*>(fec_pkt.data());
                        fec_hdr->type = PktType::FEC;
                        fec_hdr->seq = 0; 
                        fec_hdr->block_base = htonl(seq - 1); 
                        fec_hdr->timestamp = current_time_s();

                        for (ssize_t i = 0; i < n; i++) {
                            fec_pkt[sizeof(TransportHeader) + i] = prev[i] ^ buf[i];
                        }
                        sendto(relay_fd, fec_pkt.data(), fec_pkt.size(), 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));
                    }
                }
            }
        }

        // B. Drain Feedback Source (ACK/NACK processing)
        if (fds[1].revents & POLLIN) {
            for (;;) {
                ssize_t n = recvfrom(feedback_fd, buf, sizeof(buf), MSG_DONTWAIT, nullptr, nullptr);
                if (n < 0) break; 

                if (n >= static_cast<ssize_t>(sizeof(TransportHeader))) {
                    TransportHeader* hdr = reinterpret_cast<TransportHeader*>(buf);
                    
                    // --- ACK Processing (Telemetry & Memory Cleanup) ---
                    if (hdr->type == PktType::ACK) {
                        uint32_t confirmed_seq = ntohl(hdr->seq);
                        
                        double rtt = current_time_s() - hdr->timestamp;
                        smoothed_rtt = (0.875 * smoothed_rtt) + (0.125 * rtt); 
                        
                        // Garbage Collection: 10-packet safety watermark for FEC logic
                        uint32_t gc_watermark = (confirmed_seq > 10) ? (confirmed_seq - 10) : 0;
                        auto it = window.begin();
                        while (it != window.end()) {
                            if (it->first < gc_watermark) {
                                it = window.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    
                    // --- NACK Processing (Retransmissions) ---
                    else if (hdr->type == PktType::NACK) {
                        uint32_t missed_seq = ntohl(hdr->block_base);
                        
                        if (window.count(missed_seq)) {
                            const auto& payload = window[missed_seq];
                            std::vector<uint8_t> out_pkt(sizeof(TransportHeader) + payload.size());
                            
                            TransportHeader* out_hdr = reinterpret_cast<TransportHeader*>(out_pkt.data());
                            out_hdr->type = PktType::MEDIA;
                            out_hdr->seq = htonl(missed_seq);
                            out_hdr->block_base = 0;
                            out_hdr->timestamp = current_time_s(); 
                            
                            std::memcpy(out_pkt.data() + sizeof(TransportHeader), payload.data(), payload.size());
                            sendto(relay_fd, out_pkt.data(), out_pkt.size(), 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));
                        }
                    }
                }
            }
        }
    }
    return 0;
}