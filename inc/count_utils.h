static inline int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

struct StageStat {
    const char* name;
    int64_t sum_us = 0;
    int64_t max_us = 0;
    int cnt = 0;

    StageStat() : name("") {}
    explicit StageStat(const char* n) : name(n) {}
    
    void add(int64_t us) {
        sum_us += us;
        max_us = std::max(max_us, us);
        cnt++;
    }
    void reset() { sum_us = 0; max_us = 0; cnt = 0; }
};