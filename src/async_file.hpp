#pragma once
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

// Atomic file replacement off the render thread. A stalled disk must not miss a vblank.
class AsyncFile {
    struct Job { std::string path, body; };
    std::mutex mutex;
    std::condition_variable work, drained;
    std::deque<Job> jobs;
    int pending = 0;
    bool stopping = false;
    std::thread thread;
    static void store(const Job& job) {
        const auto temp = job.path + ".tmp";
        { std::ofstream file(temp); file << job.body; }
        std::rename(temp.c_str(), job.path.c_str());
    }
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                work.wait(lock, [&] { return stopping || !jobs.empty(); });
                if (jobs.empty()) return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            store(job);
            std::lock_guard lock(mutex);
            if (--pending == 0) drained.notify_all();
        }
    }
    AsyncFile() : thread([this] { run(); }) {}
public:
    static AsyncFile& instance() {
        static AsyncFile writer;
        return writer;
    }
    ~AsyncFile() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        work.notify_one();
        thread.join();
    }
    AsyncFile(const AsyncFile&) = delete;
    AsyncFile& operator=(const AsyncFile&) = delete;
    void write(std::string path, std::string body) {
        {
            std::lock_guard lock(mutex);
            jobs.push_back({std::move(path), std::move(body)});
            ++pending;
        }
        work.notify_one();
    }
    void flush() {
        std::unique_lock lock(mutex);
        drained.wait(lock, [&] { return pending == 0; });
    }
};
