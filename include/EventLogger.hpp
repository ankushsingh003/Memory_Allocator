#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace OS {

    // Emits one JSON object per line (JSONL). We build the JSON by hand rather
    // than pulling in a JSON library, since the schema is small and fixed.
    // The frontend visualizer reads this file to replay the run.
    class EventLogger {
    public:
        explicit EventLogger(const std::string& path) : out_(path) {
            out_ << "[\n";
        }

        ~EventLogger() {
            out_ << "\n]\n";
        }

        // field pairs must alternate key,value as strings; numeric values are
        // passed already-stringified by the caller (kept intentionally simple).
        void Log(int tick, const std::string& type, int pid,
                 const std::vector<std::pair<std::string, std::string>>& fields = {}) {
            if (!first_) out_ << ",\n";
            first_ = false;

            out_ << "  {"
                 << "\"tick\":" << tick << ","
                 << "\"type\":\"" << type << "\","
                 << "\"pid\":" << pid;

            for (const auto& [key, value] : fields) {
                out_ << ",\"" << key << "\":" << value;
            }
            out_ << "}";
        }

        static std::string Str(const std::string& s) { return "\"" + s + "\""; }
        static std::string Num(long long n) { return std::to_string(n); }

    private:
        std::ofstream out_;
        bool first_ = true;
    };

} // namespace OS
