#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>

/**
 * CLARIFICATION:
 * to be honest, never did socket programming for Windows before, so I had to look up for
 * the necessary headers and functions for this task. I also had to ensure that the 
 * code compiles and runs correctly on both Windows and Linux systems
 * which required some platform-specific handling for sockets and thread management
 * 
 * Compild and tested on Windows 11 only. Hopefully, it works on Linux as well without any modifications
 */

/**
 * EXPLANATION: 
 * platform-specific headers for networking and socket management
 * on Windows, we use Winsock2, while on Linux systems we use the standard socket API
 * we also define a common type `socket_t` for the socket descriptor
 * handle cleanup appropriately in the destructor of UdpManager
 */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") 
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET -1
#endif

class UdpManager {

    private:
    
        std::string target_ip;
        int target_port;
        
        /**
         * EXPLANATION:
         * Atomic flag to signal threads to stop. This allows for an immediate shutdown mechanism 
         * where all threads can be notified to exit as soon as possible, 
         * even if they are currently waiting on a condition variable.
         */
        std::atomic<bool> keep_running{true};
        std::vector<std::thread> background_threads;
        
        // Thread synchronization primitives
        std::mutex mtx;
        std::condition_variable cv;

        /**
         * EXPLANATION:
         * helper function to send a UDP packet. 
         * this function is used by all three sending methods (immediate, delayed, periodic)
         */
        void send_udp_packet(const std::string& message) {
            socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock == INVALID_SOCKET) return;

            sockaddr_in dest_addr;
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(target_port);
            inet_pton(AF_INET, target_ip.c_str(), &dest_addr.sin_addr);

            sendto(sock, message.c_str(), message.length(), 0, 
                (sockaddr*)&dest_addr, sizeof(dest_addr));

    #ifdef _WIN32
            closesocket(sock);
    #else
            close(sock);
    #endif
        }

    public:

        UdpManager(const std::string& ip, int port) : target_ip(ip), target_port(port) {

            #ifdef _WIN32
                    WSADATA wsaData;
                    WSAStartup(MAKEWORD(2, 2), &wsaData);
            #endif

        }

        /**
         * EXPLANATION:
         * the destructor is responsible for ensuring that all background threads are properly
         * signaled to stop and joined before the program exits. This is crucial for preventing
         * resource leaks and ensuring a clan shutdown, especially when threads may be waiting on
         * a condition variable
         */
        ~UdpManager() {

            // 1. Signal all threads that we are shutting down
            keep_running = false;
            
            // 2. Wake up ANY threads currently waiting on the condition variable
            cv.notify_all(); 
            
            // 3. Join threads gracefully
            for (auto& t : background_threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            #ifdef _WIN32
                    WSACleanup();
            #endif
        }

        /**
         * EXPLANATION:
         * this method sends a UDP packet immediately without any delay
         * it calls the helpr function `send_udp_packt
         */
        void sendImmediate(const std::string& msg) {
            send_udp_packet(msg);
            std::cout << "[Immediate] Sent: " << msg << std::endl;
        }

        /**
         * EXPLANATION:
         * this method sends a UDP packet after a specified delay in seconds
         */
        void sendDelayed(const std::string& msg, int delay_seconds) {
            if (delay_seconds < 1 || delay_seconds > 255) return;

            background_threads.emplace_back([this, msg, delay_seconds]() {
                std::unique_lock<std::mutex> lock(mtx);
                
                // wait_for returns TRUE if the predicate (!keep_running) is met before the timeout.
                // It returns FALSE if the timeout expires.
                if (!cv.wait_for(lock, std::chrono::seconds(delay_seconds), [this]{ return !keep_running.load(); })) {
                    // Timeout expired normally, send the message
                    send_udp_packet(msg);
                    std::cout << "[Delayed " << delay_seconds << "s] Sent: " << msg << std::endl;
                }
            });
        }

        /**
         * EXPLANATION:
         * this method sends a UDP packet periodically at specified intervals in seconds
         */
        void sendPeriodic(const std::string& msg, int interval_seconds) {
            if (interval_seconds < 1 || interval_seconds > 255) return;

            background_threads.emplace_back([this, msg, interval_seconds]() {
                std::unique_lock<std::mutex> lock(mtx);
                
                while (keep_running) {
                    // Wait for the interval. Wake up immediately if keep_running becomes false.
                    if (cv.wait_for(lock, std::chrono::seconds(interval_seconds), [this]{ return !keep_running.load(); })) {
                        // Predicate met (keep_running == false), exit the loop
                        break; 
                    }
                    
                    // Timeout expired, send the message
                    send_udp_packet(msg);
                    std::cout << "[Periodic " << interval_seconds << "s] Sent: " << msg << std::endl;
                }
            });
        }
};

int main() {
    UdpManager manager("127.0.0.1", 8080);

    manager.sendImmediate("Message 1: Immediate");
    manager.sendDelayed("Message 2: Delayed by 3s", 3);
    manager.sendPeriodic("Message 3: Periodic every 2s", 2);

    std::cout << "All tasks running. Press ENTER to initiate instant shutdown.\n";
    std::cin.get();

    std::cout << "Shutting down immediately...\n";
    return 0; // Destructor handles instantaneous thread cleanup
}