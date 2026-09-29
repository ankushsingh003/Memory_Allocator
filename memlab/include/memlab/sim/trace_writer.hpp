#pragma once
#include <fstream>
#include <string>
#include <vector>

namespace memlab {

class TraceWriter {
public:
    explicit TraceWriter(const std::string& path) : _out(path) {
        _out << "[\n";
    }

    ~TraceWriter() {
        _out << "\n]\n";
    }

    void log(int tick, const std::string& type, int pid,
             const std::vector<std::pair<std::string, std::string>>& fields = {}) {
        if (!_first) _out << ",\n";
        _first = false;

        _out << "  {"
             << "\"tick\":" << tick << ","
             << "\"type\":\"" << type << "\","
             << "\"pid\":" << pid;

        for (const auto& [k, v] : fields) {
            _out << ",\"" << k << "\":" << v;
        }
        _out << "}";
    }

    static std::string str(const std::string& s) { return "\"" + s + "\""; }
    static std::string num(long long n) { return std::to_string(n); }

private:
    std::ofstream _out;
    bool _first{true};
};

} // namespace memlab
