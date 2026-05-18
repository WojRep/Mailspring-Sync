//
//  SPDLogExtensions.hpp
//  MailSync
//
//  Created by Ben Gotow on 8/30/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef SPDLogExtensions_h
#define SPDLogExtensions_h

std::mutex spdFlushMtx;
std::condition_variable spdFlushCV;
int spdUnflushed = 0;
bool spdFlushExit = false;

void runFlushLoop() {
    while (true) {
        std::chrono::system_clock::time_point desiredTime = std::chrono::system_clock::now();
        desiredTime += chrono::milliseconds(30000);
        {
            // Wait for a message, or for 30 seconds, whichever happens first
            unique_lock<mutex> lck(spdFlushMtx);
            spdFlushCV.wait_until(lck, desiredTime);
            if (spdFlushExit) {
                return;
            }
            if (spdUnflushed == 0) {
                continue; // detect, avoid spurious wakes
            }
        }
        
        // Debounce 1sec for more messages to arrive
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        {
            // Perform flush
            unique_lock<mutex> lck(spdFlushMtx);
            spdlog::get("logger")->flush();
            spdUnflushed = 0;
        }
    }
}

class SPDFlusherSink : public spdlog::sinks::sink {
public:
    std::thread * flushThread;
    
    SPDFlusherSink() {
        flushThread = new std::thread(runFlushLoop);
        
    }
    ~SPDFlusherSink() {
        lock_guard<mutex> lck(spdFlushMtx);
        spdFlushExit = true;
        spdFlushCV.notify_one();
    }
    
    void log(const spdlog::details::log_msg& msg) {
        // ensure we have a flush queued
        lock_guard<mutex> lck(spdFlushMtx);
        spdUnflushed += 1;
        spdFlushCV.notify_one();
    }
    
    void flush() {
        // no-op
    }
};

class SPDFormatterWithThreadNames : public spdlog::pattern_formatter {
public:
    SPDFormatterWithThreadNames(const std::string& pattern) : spdlog::pattern_formatter(pattern) {}

    void format(spdlog::details::log_msg& msg) override {
        msg.logger_name = GetThreadName(msg.thread_id);
        spdlog::pattern_formatter::format(msg);
    }
};

// Ticket #04 04d — Mandarynka logger. Emits each record as a single
// pino-compatible JSON line ({level, time, name, msg}) so the mailsync C++
// log file shares one schema with the Electron-side pino logger.
class SPDJsonFormatter : public spdlog::formatter {
public:
    void format(spdlog::details::log_msg& msg) override {
        // spdlog level (trace=0 … critical=5, off=6) → pino numeric level.
        static const int pinoLevels[] = { 10, 20, 30, 40, 50, 60, 60 };
        int lvl = static_cast<int>(msg.level);
        if (lvl < 0 || lvl > 6) {
            lvl = 2; // default to info
        }

        const std::string * threadName = GetThreadName(msg.thread_id);

        nlohmann::json record;
        record["level"] = pinoLevels[lvl];
        record["time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.time.time_since_epoch()).count();
        record["name"] = threadName ? *threadName : std::string("mailsync");
        record["msg"] = msg.raw.str();

        msg.formatted << record.dump() << spdlog::details::os::eol;
    }
};



#endif /* SPDLogExtensions_h */
