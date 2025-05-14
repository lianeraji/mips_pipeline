#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iomanip>
using namespace std;

#define RESET     "\033[0m"
#define IF_COLOR  "\033[38;5;189m"
#define RR_COLOR  "\033[38;5;189m"
#define EX_COLOR  "\033[38;5;183m"
#define INST_COLOR  "\033[38;5;183m"
#define MA_COLOR  "\033[38;5;151m"
#define WR_COLOR  "\033[38;5;186m"
#define ST_COLOR  "\033[38;5;203m"

vector<string> STAGES = {"IF", "RR", "EX", "MA", "WR"};
vector<string> STAGE_COLORS = {IF_COLOR, RR_COLOR, EX_COLOR, MA_COLOR, WR_COLOR};

struct Instruction {
    string raw;
    string opcode, rd, rs1, rs2;
};

bool isLoad(const Instruction& instr) {
    return instr.opcode == "lw";
}

bool hasDependency(const Instruction& a, const Instruction& b) {
    return (a.rd == b.rs1 || a.rd == b.rs2 || b.rd == a.rs1 || b.rd == a.rs2 || a.rd == b.rd);
}

void stallonoff(vector<Instruction>& instrs, bool forwarding) {
    auto hasConflict = [](const Instruction& a, const Instruction& b) {
        return (a.rd == b.rs1 || a.rd == b.rs2 ||
                b.rd == a.rs1 || b.rd == a.rs2 ||
                a.rd == b.rd);
    };

    size_t n = instrs.size();
    for (size_t i = 0; i < n - 1; ++i) {
        Instruction& current = instrs[i];
        Instruction& next = instrs[i + 1];

        bool hazard = (current.rd == next.rs1 || current.rd == next.rs2);

        if (!hazard) continue;
        if (forwarding && current.opcode != "lw") continue;
        if (forwarding && current.opcode == "lw" && next.opcode != "lw") continue;

        for (size_t j = i + 1; j < n; ++j) {
            bool safe = true;
            for (size_t k = i + 1; k <= j; ++k) {
                if (hasConflict(current, instrs[k])) {
                    safe = false;
                    break;
                }
            }
            if (safe) {
                Instruction temp = current;
                for (size_t k = i; k < j; ++k)
                    instrs[k] = instrs[k + 1];
                instrs[j] = temp;
                break;
            }
        }
    }
}

class PipelineSimulator {
private:
    vector<Instruction> instructions;
    vector<vector<string>> pipelineTable;
    vector<int> pipelineStage;
    vector<bool> started;
    vector<int> stallUntil;
    bool forwarding;
    int totalCycles;
    int stallCount = 0;

public:
    PipelineSimulator(const vector<Instruction>& instrs, bool fwd)
        : instructions(instrs), forwarding(fwd), totalCycles(0) {
        pipelineTable.resize(instrs.size(), vector<string>(50, "-"));
        pipelineStage.resize(instrs.size(), -1);
        started.resize(instrs.size(), false);
        stallUntil.resize(instrs.size(), 0);
    }

    bool hasHazard(size_t i) {
        if (i == 0) return false;
        Instruction curr = instructions[i];
        for (size_t j = max(0, static_cast<int>(i) - 2); j < i; ++j) {
            Instruction prev = instructions[j];
            if (prev.rd == curr.rs1 || prev.rd == curr.rs2) {
                int prevStage = pipelineStage[j];
                if (forwarding) {
                    if (isLoad(prev) && prevStage < 3) return true;
                    else if (!isLoad(prev) && prevStage < 2) return true;
                } else {
                    if (prevStage < 4) return true;
                }
            }
        }
        return false;
    }

    void simulate() {
        int maxCycles = 50;
        for (int cycle = 0; cycle < maxCycles; ++cycle) {
            vector<bool> stall(instructions.size(), false);

            if (!forwarding) {
                for (size_t i = 0; i < instructions.size(); ++i)
                    if (pipelineStage[i] == 1 && hasHazard(i)) {
                        stall[i] = true;
                        stallUntil[i] = cycle + 2;
                    }
            }

            for (size_t i = 0; i < instructions.size(); ++i) {
                if (pipelineStage[i] == -1 && !started[i]) {
                    if (i == 0 || (pipelineStage[i - 1] > 1 && pipelineTable[i - 1][cycle - 1] != "ST")) {
                        pipelineStage[i] = 1;
                        started[i] = true;
                        pipelineTable[i][cycle] = STAGES[0];
                    }
                } else if (pipelineStage[i] >= 0 && static_cast<size_t>(pipelineStage[i]) < STAGES.size()) {
                    if ((stall[i] || cycle < stallUntil[i])) {
                        pipelineTable[i][cycle] = "ST";
                        stallCount++;
                    } else {
                        pipelineTable[i][cycle] = STAGES[pipelineStage[i]];
                        pipelineStage[i]++;
                    }
                }
            }

            for (size_t i = 1; i < instructions.size(); ++i)
                if (pipelineTable[i - 1][cycle] == "ST" && pipelineTable[i][cycle] != "ST" && pipelineTable[i][cycle] != STAGES[0])
                    if (pipelineStage[i] >= 0) pipelineTable[i][cycle] = "ST";

            if (all_of(pipelineStage.begin(), pipelineStage.end(), [](int s) { return s >= 5; })) {
                totalCycles = cycle + 1;
                break;
            }
        }
        printResults();
    }

    void printResults() {
        cout << "\n\n⋅°₊ • ୨୧ ‧₊° ⋅˖ ݁𖥔 ݁˖   𐙚   ˖ ݁𖥔 ݁˖ ⋅°₊ • ୨୧ ‧₊° ⋅  \n˚₊‧ 𐙚 ‧₊˚ ⋅🎀⋅˚₊‧ 𐙚 ‧₊˚ ⋅ PIPELINE TIMELINE ⋅˚₊‧ 𐙚 ‧₊˚ ⋅🎀⋅˚₊‧ 𐙚 ‧₊˚ ⋅\n⋅°₊ • ୨୧ ‧₊° ⋅˖ ݁𖥔 ݁˖   𐙚   ˖ ݁𖥔 ݁˖ ⋅°₊ • ୨୧ ‧₊° ⋅ \n\n";

        cout << "+------------------------------+";
        for (int i = 0; i < totalCycles; ++i) cout << "------+";
        cout << "\n|" << setw(29) << "Instruction" << " |";
        for (int i = 0; i < totalCycles; ++i) cout << " C " << setw(2)<<i + 1 << " |";
        cout << "\n+------------------------------+";
        for (int i = 0; i < totalCycles; ++i) cout << "------+";
        cout << "\n";

        for (size_t i = 0; i < instructions.size(); ++i) {
            cout << "|" << setw(29) << instructions[i].raw  << " |";
            for (int j = 0; j < totalCycles; ++j) {
                string val = pipelineTable[i][j];
                if (val == "ST") cout << ST_COLOR << setw(6) << "ST" << RESET << "|";
                else if (val == "-") cout << setw(6) << " " << "|";
                else {
                    size_t idx = find(STAGES.begin(), STAGES.end(), val) - STAGES.begin();
                    cout << STAGE_COLORS[idx] << setw(6) << val << RESET << "|";
                }
            }
            cout << "\n+------------------------------+";
            for (int j = 0; j < totalCycles; ++j) cout << "------+";
            cout << "\n";
        }

        double throughput = static_cast<double>(instructions.size()) / totalCycles;
        double speedup = static_cast<double>(instructions.size() * STAGES.size()) / totalCycles;

        cout << "\n｡･ﾟﾟ･\t୨୧\t･ﾟﾟ･｡\n⋆｡˚ ❀  STATISTICS  ❀ ˚｡⋆\n｡･ﾟﾟ･\t୨୧\t･ﾟﾟ･｡\n\n";
        cout << "୨୧ Total stalls     : " << ST_COLOR << stallCount << RESET << "\n";
        cout << "୨୧ Total cycles     : " << totalCycles << "\n";
        cout << "୨୧ Total time       : " << 200 * totalCycles << " ps\n";
        cout << "୨୧ Instructions     : " << instructions.size() << "\n";
        cout << "୨୧ Throughput       : " << fixed << setprecision(2) << throughput << " instr/cycle\n";
        cout << "୨୧ Speedup          : " << fixed << setprecision(2) << speedup << "x\n\n";
    }
};

Instruction parseInstruction(const string& line) {
    string temp = line;
    replace(temp.begin(), temp.end(), ',', ' ');
    replace(temp.begin(), temp.end(), '(', ' ');
    replace(temp.begin(), temp.end(), ')', ' ');
    stringstream ss(temp);
    Instruction instr;
    instr.raw = line;
    ss >> instr.opcode >> instr.rd >> instr.rs1 >> instr.rs2;
    return instr;
}

int main() {
    ifstream infile("test.asm");
    if (!infile) {
        cerr << "❌ Error: Could not open test.asm\n";
        return 1;
    }

    vector<Instruction> instructions;
    string line;
    while (getline(infile, line)) {
        if (!line.empty())
            instructions.push_back(parseInstruction(line));
    }
    infile.close();

    cout << "୨୧ Forward?: ";
    string fwdInput;
    cin >> fwdInput;
    bool forwarding = (fwdInput == "1" || fwdInput == "yes");

    cout << "୨୧ Reorder?: ";
    string reorderInput;
    cin >> reorderInput;
    bool reordering = (reorderInput == "1" || reorderInput == "yes");

    if (reordering)
        stallonoff(instructions, forwarding);

    PipelineSimulator sim(instructions, forwarding);
    sim.simulate();
    return 0;
}