/*
 * Virtual Memory Management Simulator
 * -----------------------------------
 * Console-based simulator for demand-paging page replacement policies:
 *   1. FIFO   (First-In First-Out)
 *   2. LRU    (Least Recently Used)
 *   3. OPT    (Optimal / Belady's MIN)
 *
 * Author  : Devansh Gupta (Roll No. 25ET2041011)
 * Course  : B.Tech CSE, Semester 3 - Operating Systems Mini Project
 * Build   : g++ -std=c++17 -O2 -Wall -Wextra vmm_simulator.cpp -o vmm
 */

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int EMPTY_FRAME   = -1;   // marker for an unoccupied frame
constexpr int MAX_REF_LEN   = 100;  // maximum length of reference string
constexpr int MAX_PAGE_NO   = 999;  // maximum legal page number
constexpr int MAX_FRAMES    = 10;   // maximum number of physical frames

/* One row of the simulation trace (state after serving one reference). */
struct Step {
    int              page;     // page referenced at this step
    std::vector<int> frames;   // frame contents after the reference
    bool             fault;    // true -> page fault, false -> page hit
    int              victim;   // page evicted (EMPTY_FRAME if none)
};

/* Aggregate outcome of one algorithm run. */
struct Result {
    std::string       name;
    std::vector<Step> trace;
    int               faults    = 0;
    int               hits      = 0;
    double            hitRatio  = 0.0;   // percentage
    double            faultRate = 0.0;   // percentage
};

}  // namespace

/* ------------------------------------------------------------------ */
/*  Simulator core                                                     */
/* ------------------------------------------------------------------ */
class MemorySimulator {
public:
    bool setReferenceString(const std::vector<int>& refs) {
        if (refs.empty() || static_cast<int>(refs.size()) > MAX_REF_LEN) return false;
        for (int p : refs)
            if (p < 0 || p > MAX_PAGE_NO) return false;
        refs_ = refs;
        return true;
    }

    bool setFrameCount(int n) {
        if (n < 1 || n > MAX_FRAMES) return false;
        frames_ = n;
        return true;
    }

    bool ready() const { return !refs_.empty() && frames_ > 0; }
    int  frameCount() const { return frames_; }
    const std::vector<int>& refs() const { return refs_; }

    /* FIFO: evict the page that has been resident the longest.        */
    Result runFIFO() const {
        Result r = begin("FIFO");
        std::vector<int> mem(frames_, EMPTY_FRAME);
        int next = 0;                          // circular pointer = oldest frame
        for (int page : refs_) {
            bool hit = std::find(mem.begin(), mem.end(), page) != mem.end();
            int victim = EMPTY_FRAME;
            if (!hit) {
                victim = mem[next];
                mem[next] = page;
                next = (next + 1) % frames_;
            }
            record(r, page, mem, !hit, victim);
        }
        return finish(r);
    }

    /* LRU: evict the page whose last use is furthest in the past.     */
    Result runLRU() const {
        Result r = begin("LRU");
        std::vector<int> mem(frames_, EMPTY_FRAME);
        std::vector<int> lastUsed(frames_, -1);
        for (int t = 0; t < static_cast<int>(refs_.size()); ++t) {
            int page = refs_[t];
            auto it  = std::find(mem.begin(), mem.end(), page);
            int victim = EMPTY_FRAME;
            bool hit = (it != mem.end());
            if (hit) {
                lastUsed[it - mem.begin()] = t;
            } else {
                int slot = pickEmptySlot(mem);
                if (slot < 0) {                // memory full: find LRU slot
                    slot = static_cast<int>(
                        std::min_element(lastUsed.begin(), lastUsed.end()) - lastUsed.begin());
                    victim = mem[slot];
                }
                mem[slot] = page;
                lastUsed[slot] = t;
            }
            record(r, page, mem, !hit, victim);
        }
        return finish(r);
    }

    /* OPT: evict the page whose next use is furthest in the future.   */
    Result runOptimal() const {
        Result r = begin("Optimal");
        std::vector<int> mem(frames_, EMPTY_FRAME);
        const int n = static_cast<int>(refs_.size());
        for (int t = 0; t < n; ++t) {
            int page = refs_[t];
            bool hit = std::find(mem.begin(), mem.end(), page) != mem.end();
            int victim = EMPTY_FRAME;
            if (!hit) {
                int slot = pickEmptySlot(mem);
                if (slot < 0) {
                    int farthest = -1;
                    for (int f = 0; f < frames_; ++f) {
                        int nextUse = n;       // n means "never used again"
                        for (int k = t + 1; k < n; ++k)
                            if (refs_[k] == mem[f]) { nextUse = k; break; }
                        if (nextUse > farthest) { farthest = nextUse; slot = f; }
                    }
                    victim = mem[slot];
                }
                mem[slot] = page;
            }
            record(r, page, mem, !hit, victim);
        }
        return finish(r);
    }

private:
    std::vector<int> refs_;
    int              frames_ = 0;

    Result begin(const std::string& name) const {
        Result r;
        r.name = name;
        return r;
    }

    static int pickEmptySlot(const std::vector<int>& mem) {
        for (size_t i = 0; i < mem.size(); ++i)
            if (mem[i] == EMPTY_FRAME) return static_cast<int>(i);
        return -1;
    }

    static void record(Result& r, int page, const std::vector<int>& mem,
                       bool fault, int victim) {
        r.trace.push_back({page, mem, fault, victim});
        if (fault) ++r.faults; else ++r.hits;
    }

    Result finish(Result r) const {
        const double total = static_cast<double>(refs_.size());
        r.hitRatio  = 100.0 * r.hits   / total;
        r.faultRate = 100.0 * r.faults / total;
        return r;
    }
};

/* ------------------------------------------------------------------ */
/*  Presentation helpers                                               */
/* ------------------------------------------------------------------ */
static void printTrace(const Result& r, int frames) {
    std::cout << "\n=== " << r.name << " Page Replacement ===\n";
    std::cout << std::left << std::setw(6) << "Step" << std::setw(6) << "Ref";
    for (int f = 0; f < frames; ++f) std::cout << std::setw(6) << ("F" + std::to_string(f + 1));
    std::cout << "Status\n" << std::string(12 + 6 * frames + 12, '-') << "\n";
    int step = 1;
    for (const Step& s : r.trace) {
        std::cout << std::setw(6) << step++ << std::setw(6) << s.page;
        for (int p : s.frames) {
            if (p == EMPTY_FRAME) std::cout << std::setw(6) << "-";
            else                  std::cout << std::setw(6) << p;
        }
        std::cout << (s.fault ? "FAULT" : "HIT");
        if (s.fault && s.victim != EMPTY_FRAME) std::cout << " (evict " << s.victim << ")";
        std::cout << "\n";
    }
    std::cout << std::fixed << std::setprecision(2)
              << "Total Page Faults : " << r.faults << "\n"
              << "Total Page Hits   : " << r.hits << "\n"
              << "Hit Ratio         : " << r.hitRatio << " %\n"
              << "Fault Rate        : " << r.faultRate << " %\n";
}

static void printComparison(const std::vector<Result>& all, size_t refLen, int frames) {
    std::cout << "\n=== Performance Comparison (" << refLen << " references, "
              << frames << " frames) ===\n";
    std::cout << std::left << std::setw(12) << "Algorithm" << std::setw(8) << "Faults"
              << std::setw(8) << "Hits" << std::setw(12) << "Hit Ratio" << "Fault Rate\n"
              << std::string(52, '-') << "\n";
    for (const Result& r : all) {
        std::ostringstream hr, fr;                       // fixed 2-decimal percentages
        hr << std::fixed << std::setprecision(2) << r.hitRatio  << "%";
        fr << std::fixed << std::setprecision(2) << r.faultRate << "%";
        std::cout << std::setw(12) << r.name << std::setw(8) << r.faults << std::setw(8)
                  << r.hits << std::setw(12) << hr.str() << fr.str() << "\n";
    }
    const int minFaults = std::min_element(all.begin(), all.end(),
        [](const Result& a, const Result& b) { return a.faults < b.faults; })->faults;
    std::string winners;                                  // all algorithms sharing the minimum
    int count = 0;
    for (const Result& r : all)
        if (r.faults == minFaults) { winners += (count++ ? ", " : "") + r.name; }
    if (count == static_cast<int>(all.size()))
        std::cout << "Result: all algorithms tie (" << minFaults << " faults)\n";
    else
        std::cout << "Best performer: " << winners << " (" << minFaults << " faults)\n";
}

static bool writeReport(const std::string& path, const std::vector<Result>& all,
                        const std::vector<int>& refs, int frames) {
    std::ofstream out(path);
    if (!out) return false;
    out << "VIRTUAL MEMORY MANAGEMENT SIMULATOR - RESULT REPORT\n"
        << "Frames: " << frames << "\nReference string:";
    for (int p : refs) out << ' ' << p;
    out << "\n\nAlgorithm   Faults  Hits  HitRatio(%)  FaultRate(%)\n";
    out << std::fixed << std::setprecision(2);
    for (const Result& r : all)
        out << std::left << std::setw(12) << r.name << std::setw(8) << r.faults
            << std::setw(6) << r.hits << std::setw(13) << r.hitRatio << r.faultRate << "\n";
    return true;
}

/* ------------------------------------------------------------------ */
/*  Input handling                                                     */
/* ------------------------------------------------------------------ */
static bool parseInts(const std::string& line, std::vector<int>& out) {
    std::string s = line;
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream iss(s);
    std::string tok;
    out.clear();
    while (iss >> tok) {
        size_t pos = 0;
        try {
            int v = std::stoi(tok, &pos);
            if (pos != tok.size()) return false;
            out.push_back(v);
        } catch (...) {
            return false;
        }
    }
    return !out.empty();
}

static void showMenu() {
    std::cout << "\n========== VIRTUAL MEMORY MANAGEMENT SIMULATOR ==========\n"
                 " 1. Enter page reference string\n"
                 " 2. Set number of frames\n"
                 " 3. FIFO page replacement\n"
                 " 4. LRU page replacement\n"
                 " 5. Optimal page replacement\n"
                 " 6. Compare all algorithms\n"
                 " 7. Generate report file\n"
                 " 8. Exit\n"
                 "Enter choice: ";
}

int main() {
    MemorySimulator sim;
    std::string line;
    while (true) {
        showMenu();
        if (!std::getline(std::cin, line)) break;
        std::vector<int> v;
        if (!parseInts(line, v) || v.size() != 1) {
            std::cout << "Invalid choice. Please enter a number between 1 and 8.\n";
            continue;
        }
        switch (v[0]) {
        case 1:
            std::cout << "Enter reference string (space/comma separated, pages 0-"
                      << MAX_PAGE_NO << "): ";
            std::getline(std::cin, line);
            if (parseInts(line, v) && sim.setReferenceString(v))
                std::cout << "Reference string accepted (" << v.size() << " references).\n";
            else
                std::cout << "Error: invalid reference string.\n";
            break;
        case 2:
            std::cout << "Enter number of frames (1-" << MAX_FRAMES << "): ";
            std::getline(std::cin, line);
            if (parseInts(line, v) && v.size() == 1 && sim.setFrameCount(v[0]))
                std::cout << "Frame count set to " << v[0] << ".\n";
            else
                std::cout << "Error: invalid frame count.\n";
            break;
        case 3: case 4: case 5: {
            if (!sim.ready()) { std::cout << "Please set the reference string and frames first.\n"; break; }
            Result r = (v[0] == 3) ? sim.runFIFO() : (v[0] == 4) ? sim.runLRU() : sim.runOptimal();
            printTrace(r, sim.frameCount());
            break;
        }
        case 6: {
            if (!sim.ready()) { std::cout << "Please set the reference string and frames first.\n"; break; }
            std::vector<Result> all{sim.runFIFO(), sim.runLRU(), sim.runOptimal()};
            printComparison(all, sim.refs().size(), sim.frameCount());
            break;
        }
        case 7: {
            if (!sim.ready()) { std::cout << "Please set the reference string and frames first.\n"; break; }
            std::vector<Result> all{sim.runFIFO(), sim.runLRU(), sim.runOptimal()};
            std::cout << (writeReport("vm_report.txt", all, sim.refs(), sim.frameCount())
                          ? "Report written to vm_report.txt\n" : "Error: could not write report.\n");
            break;
        }
        case 8:
            std::cout << "Exiting simulator. Goodbye!\n";
            return 0;
        default:
            std::cout << "Invalid choice. Please enter a number between 1 and 8.\n";
        }
    }
    return 0;
}
