#include <chrono>
#include <atomic>
// #include <memory> // <- Not required in this code, can be removed
#include <thread>
#include <functional>
#include <iostream>

using namespace std::chrono_literals;

/**
 * What is this code doing?
 * Starts a thread that executes the provided Process function in a loop until either:
 * The Process function returns true, indicating an abort condition.
 */

void StartThread(
    std::thread& thread,
    std::atomic<bool>& running,
    /**
     * MAJOR ISSUE:
     * Because StartThread creates the thread and returns immediately, its local parameters (Process and timeout) are destroyed right after the thread is spawned
     * the newly spawned thread continues running in the background, trying to invoke Process() and check duration > timeout
     * since those variables no longer exist in memory, the thread accesses freed memory (a dangling reference), causing a crash or unpredictable behavior.
     */
    const std::function<bool(void)> Process, // FIX: Pass Process by value to ensure it remains valid for the thread's lifetime
    const std::chrono::seconds timeout)
{
    thread = std::thread(
        [Process, &running, timeout]()
        {
            auto start = std::chrono::high_resolution_clock::now();
            while(running)
            {
                bool aborted = Process();

                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                if (aborted || duration > timeout)
                {
                    running = false;
                    break;
                }
            }
        });
}

int main(int argc, char **argv)
{
    /**
     * EXPLANATION:
     * atomic boolean variable (my_running) is used to control the execution of both threads (my_thread1 and my_thread2) 
     * This variable is shared between the threads, and when one thread sets it to false, it signals the other thread to stop as well
     */
    std::atomic<bool> my_running = true;
    std::thread my_thread1, my_thread2;
    /**
     * IMPROVEMENT SUGGESTION:
     * atomic counters to avoid race conditions when incrementing for 1 thread and reading for the main thread 
     * even though we are using join() to wait for the threads to finish
     */      
    std::atomic<int> loop_counter1 = 0, loop_counter2 = 0; 

    /**
     * EXPLANATION:
     * we have two threads (my_thread1 and my_thread2) that execute counter increment in a loop
     * my_thread1 will increment loop_counter1 every 2 seconds until it reaches 10 seconds or the Process function returns true (which it never does in this case)     
     */
    // start actions in seprate threads and wait of them
    StartThread(
        my_thread1,
        my_running, 
        [&]()
        {
            // "some actions" simulated with waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            loop_counter1++;
            return false;
        },
        10s); // loop timeout
    
    /**
     * EXPLANATION:
     * my_thread2 will increment loop_counter2 every 1 second until it reaches 5 increments, at which point it will return true and signal the thread to stop. 
     * However, since both threads share the same atomic boolean (my_running), when my_thread2 sets it to false, it will also cause my_thread1 to stop, even if it hasn't reached its timeout or completed its intended number of increments. 
     * This means that the behavior of my_thread1 is unintentionally affected by the actions of my_thread2, leading to a potential race condition and unintended consequences.
     */
    StartThread(
        my_thread2,
        my_running, 
        [&]()
        {
            // "some actions" simulated with waiting 
            if (loop_counter2 < 5)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                loop_counter2++;
                return false;
            }
            return true;
        },
        10s); // loop timeout

    /**
     * IMPROVEMENT SUGGESTION:
     * Use joinable() to check if the thread is joinable before calling join() to avoid potential issues if the thread was not started or has already been joined. 
     */
    my_thread1.join();
    my_thread2.join();

    // print execlution loop counters
    std::cout << "C1: " << loop_counter1 << " C2: " << loop_counter2 << std::endl;

    // wait for user input before exiting
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    return 0;
}
