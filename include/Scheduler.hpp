#pragma once
#include "Process.hpp"
#include "BuddyAllocator.hpp"
#include "EventLogger.hpp"
#include <deque>
#include <vector>
#include <algorithm>

namespace OS {

    // A simple round-robin CPU scheduler with memory-constrained admission:
    // a process only leaves NEW and enters READY once the (real) BuddyAllocator
    // can actually grant its memory request. This models admission control /
    // memory pressure, not just CPU scheduling in isolation.
    class Scheduler {
    public:
        Scheduler(Memory::BuddyAllocator& allocator, EventLogger& log, int quantum = 3)
            : allocator_(allocator), log_(log), quantum_(quantum) {}

        void AddProcess(Process p) {
            allProcesses_.push_back(std::move(p));
        }

        // Runs the simulation until every process has terminated or maxTicks
        // is reached (safety valve against infinite loops in a bad config).
        void Run(int maxTicks = 500) {
            for (tick_ = 0; tick_ < maxTicks; ++tick_) {
                AdmitPendingProcesses();
                ServiceIoQueue();
                RunOneCpuSlice();

                if (AllTerminated()) break;
            }
        }

    private:
        Memory::BuddyAllocator& allocator_;
        EventLogger& log_;
        int quantum_;
        int tick_ = 0;

        std::vector<Process> allProcesses_;
        std::deque<int> readyQueue_;   // indices into allProcesses_
        std::vector<int> waitingList_; // indices into allProcesses_ (I/O)
        int runningIdx_ = -1;

        bool AllTerminated() const {
            return std::all_of(allProcesses_.begin(), allProcesses_.end(),
                [](const Process& p) { return p.state == ProcessState::TERMINATED; });
        }

        // NEW -> READY, but only if the allocator actually has room. If not,
        // the process just waits another tick and we try again later — this
        // is the "memory-constrained scheduling" behavior.
        void AdmitPendingProcesses() {
            for (auto& p : allProcesses_) {
                if (p.state != ProcessState::NEW || p.arrivalTick > tick_) continue;

                void* ptr = allocator_.Allocate(p.memoryRequest);
                if (!ptr) {
                    // Not enough contiguous memory right now; stays NEW, retried next tick.
                    continue;
                }

                p.memPtr = ptr;
                p.memOffset = allocator_.OffsetOf(ptr);
                p.memBlockSize = allocator_.BlockSizeForRequest(p.memoryRequest);
                p.state = ProcessState::READY;

                log_.Log(tick_, "process_created", p.id, {
                    {"name", EventLogger::Str(p.name)},
                    {"memoryRequest", EventLogger::Num((long long)p.memoryRequest)}
                });
                log_.Log(tick_, "memory_allocated", p.id, {
                    {"offset", EventLogger::Num((long long)p.memOffset)},
                    {"size", EventLogger::Num((long long)p.memBlockSize)}
                });
                log_.Log(tick_, "process_ready", p.id, {});

                readyQueue_.push_back(IndexOf(p));
            }
        }

        // Ticks down every process currently blocked on I/O; moves finished
        // ones back to READY.
        void ServiceIoQueue() {
            for (auto it = waitingList_.begin(); it != waitingList_.end(); ) {
                Process& p = allProcesses_[*it];
                p.remainingIoTicks--;
                if (p.remainingIoTicks <= 0) {
                    log_.Log(tick_, "io_ack", p.id, {});
                    p.state = ProcessState::READY;
                    log_.Log(tick_, "process_ready", p.id, {});
                    readyQueue_.push_back(*it);
                    it = waitingList_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Runs the head of the ready queue for up to `quantum_` ticks (or until
        // its current CPU burst finishes, whichever comes first).
        void RunOneCpuSlice() {
            if (readyQueue_.empty()) return;

            int idx = readyQueue_.front();
            readyQueue_.pop_front();
            Process& p = allProcesses_[idx];

            p.state = ProcessState::RUNNING;
            log_.Log(tick_, "process_scheduled", p.id, {
                {"burstIndex", EventLogger::Num((long long)p.currentBurst)}
            });

            Burst& burst = p.bursts[p.currentBurst];
            if (p.remainingCpuTicks <= 0) p.remainingCpuTicks = burst.cpuTicks;

            int ran = std::min(quantum_, p.remainingCpuTicks);
            p.remainingCpuTicks -= ran;
            log_.Log(tick_, "cpu_run", p.id, {
                {"ticks", EventLogger::Num(ran)},
                {"remaining", EventLogger::Num(p.remainingCpuTicks)}
            });

            if (p.remainingCpuTicks > 0) {
                // Quantum expired but burst isn't done — preempt back to READY.
                p.state = ProcessState::READY;
                log_.Log(tick_, "process_ready", p.id, {{"reason", EventLogger::Str("preempted")}});
                readyQueue_.push_back(idx);
                return;
            }

            // Current CPU burst finished. Does it have an I/O burst after it?
            if (burst.ioTicks > 0) {
                p.state = ProcessState::WAITING;
                p.remainingIoTicks = burst.ioTicks;
                p.currentBurst++;
                log_.Log(tick_, "io_requested", p.id, {{"ioTicks", EventLogger::Num(burst.ioTicks)}});
                waitingList_.push_back(idx);
                return;
            }

            // No I/O — check if there's another CPU burst queued up.
            p.currentBurst++;
            if (p.HasMoreBursts()) {
                p.state = ProcessState::READY;
                readyQueue_.push_back(idx);
                log_.Log(tick_, "process_ready", p.id, {});
                return;
            }

            // Process is done: free its memory and terminate.
            allocator_.Free(p.memPtr, p.memBlockSize);
            log_.Log(tick_, "memory_freed", p.id, {
                {"offset", EventLogger::Num((long long)p.memOffset)},
                {"size", EventLogger::Num((long long)p.memBlockSize)}
            });
            p.state = ProcessState::TERMINATED;
            log_.Log(tick_, "process_terminated", p.id, {});
        }

        int IndexOf(const Process& p) const {
            return static_cast<int>(&p - &allProcesses_[0]);
        }
    };

} // namespace OS
