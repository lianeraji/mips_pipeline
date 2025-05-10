#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <map>
#include <string>
#include <cstdint>
#include <xlsxwriter.h>
#include <algorithm>

using namespace std;


#define CLR_RESET   "\033[0m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_MAGENTA "\033[35m"
#define CLR_CYAN    "\033[36m"
#define CLR_BOLD    "\033[1m"

struct Instruction {
    string name;
    uint32_t pc;
    string original;
    bool flushed = false;
    bool taken = false;       
    bool executed = false;    
    int fetch_cycle = -1;     
    int decode_cycle = -1;    
    int execute_cycle = -1;  
    int mem_cycle = -1;      
    int wb_cycle = -1;       

    string dest_reg = "";
    vector<string> src_regs;
};

vector<Instruction> instructions;
map<int, string> cycle_notes;  
int total_cycles = 0;
int executed_instructions = 0;
bool ENABLE_FORWARDING = true;
bool ENABLE_BRANCH_PREDICTION = true;
bool ENABLE_REORDERING = true;

const int CYCLE_TIME_PS = 200;
const int MAX_CYCLES = 1000;
const int DISPLAY_CYCLES = 50; 
map<uint32_t, bool> branch_predictor;
map<string, int> register_writer; 

string extract_register(const string& instr_text, size_t pos) {
    size_t reg_start = instr_text.find('$', pos);
    if (reg_start == string::npos) return "";
    
    size_t reg_end = min({
        instr_text.find(',', reg_start),
        instr_text.find(')', reg_start),
        instr_text.find(' ', reg_start)
    });
    
    if (reg_end == string::npos) {
        reg_end = instr_text.length();
    }
    
    return instr_text.substr(reg_start, reg_end - reg_start);
}

void reorder_instructions() {
    vector<Instruction> reordered;
    vector<bool> used(instructions.size(), false);

    for (size_t i = 0; i < instructions.size(); ++i) {
        if (used[i]) continue;

        const auto& inst = instructions[i];


        if (i + 1 < instructions.size()) {
            for (size_t j = i + 1; j < instructions.size(); ++j) {
                if (used[j]) continue;

                bool independent = true;
                for (const string& src : instructions[i].src_regs) {
                    if (instructions[j].dest_reg == src) {
                        independent = false;
                        break;
                    }
                }
                for (const string& src : instructions[j].src_regs) {
                    if (inst.dest_reg == src) {
                        independent = false;
                        break;
                    }
                }

                if (independent) {
                    reordered.push_back(instructions[j]);
                    used[j] = true;
                }
            }
        }

        reordered.push_back(inst);
        used[i] = true;
    }

    instructions = reordered;
}


void parse_instruction(Instruction& instr) {
    size_t first_dollar = instr.original.find('$');
    if (first_dollar == string::npos) return;
    
    if (instr.name == "add" || instr.name == "sub" || instr.name == "and" || 
        instr.name == "or" || instr.name == "slt" || instr.name == "mul" || 
        instr.name == "div") {
        instr.dest_reg = extract_register(instr.original, first_dollar);
        
        size_t second_dollar = instr.original.find('$', first_dollar + 1);
        if (second_dollar != string::npos) {
            instr.src_regs.push_back(extract_register(instr.original, second_dollar));
            
            size_t third_dollar = instr.original.find('$', second_dollar + 1);
            if (third_dollar != string::npos) {
                instr.src_regs.push_back(extract_register(instr.original, third_dollar));
            }
        }
    } 
    else if (instr.name == "lw" || instr.name == "lb" || instr.name == "lh") {
        instr.dest_reg = extract_register(instr.original, first_dollar);
        
        size_t paren_pos = instr.original.find('(');
        if (paren_pos != string::npos) {
            size_t rs_pos = instr.original.find('$', paren_pos);
            if (rs_pos != string::npos) {
                instr.src_regs.push_back(extract_register(instr.original, rs_pos));
            }
        }
    }
    else if (instr.name == "sw" || instr.name == "sb" || instr.name == "sh") {

        instr.src_regs.push_back(extract_register(instr.original, first_dollar));
        
        size_t paren_pos = instr.original.find('(');
        if (paren_pos != string::npos) {
            size_t rs_pos = instr.original.find('$', paren_pos);
            if (rs_pos != string::npos) {
                instr.src_regs.push_back(extract_register(instr.original, rs_pos));
            }
        }
    }
    else if (instr.name == "beq" || instr.name == "bne") {
        instr.src_regs.push_back(extract_register(instr.original, first_dollar));
        
        size_t second_dollar = instr.original.find('$', first_dollar + 1);
        if (second_dollar != string::npos) {
            instr.src_regs.push_back(extract_register(instr.original, second_dollar));
        }
    }
    else if (instr.name == "j" || instr.name == "jal") {
    }
    else {
        size_t pos = first_dollar;
        for (int i = 0; i < 3 && pos != string::npos; i++) {
            string reg = extract_register(instr.original, pos);
            if (i == 0 && instr.name[0] != 'b' && instr.name != "sw" && 
                instr.name != "sb" && instr.name != "sh") {
                instr.dest_reg = reg;
            } else {
                instr.src_regs.push_back(reg);
            }
            pos = instr.original.find('$', pos + 1);
        }
    }
}

void load_program(const string& filename) {
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << CLR_RED << "Error opening file: " << filename << CLR_RESET << endl;
        exit(EXIT_FAILURE);
    }

    string line;
    uint32_t pc = 0;
    while (getline(infile, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.find_last_not_of(" \t") != string::npos) {
            line.erase(line.find_last_not_of(" \t") + 1);
        }
        if (line.empty() || line[0] == '#') continue;
        
        Instruction instr;
        instr.pc = pc;
        instr.original = line;
        
        size_t first_space = line.find_first_of(" \t");
        instr.name = (first_space != string::npos) ? line.substr(0, first_space) : line;
        
        if (instr.name == "beq" || instr.name == "bne" || 
            instr.name == "j" || instr.name == "jal") {
            auto it = branch_predictor.find(pc);
            if (it != branch_predictor.end()) {
                instr.taken = it->second;
            } else {
                instr.taken = false; 
                branch_predictor[pc] = false;
            }
        }
        
        parse_instruction(instr);
        
        instructions.push_back(instr);
        pc += 4;
    }
    infile.close();
}

bool is_load(const string& instr_name) {
    return instr_name == "lw" || instr_name == "lb" || 
           instr_name == "lh" || instr_name == "lbu" || instr_name == "lhu";
}

bool is_branch(const string& instr_name) {
    return instr_name == "beq" || instr_name == "bne" || 
           instr_name == "j" || instr_name == "jal";
}

bool has_data_hazard(int producer_idx, int consumer_idx) {
    if (producer_idx < 0 || consumer_idx < 0 || 
        producer_idx >= (int)instructions.size() || 
        consumer_idx >= (int)instructions.size()) {
        return false;
    }
    
    const Instruction& producer = instructions[producer_idx];
    const Instruction& consumer = instructions[consumer_idx];
    
    if (!producer.dest_reg.empty()) {
        for (const auto& src_reg : consumer.src_regs) {
            if (src_reg == producer.dest_reg) {
                if (ENABLE_FORWARDING) {
                    return is_load(producer.name) && 
                          (consumer_idx == producer_idx + 1);
                }
                return true;
            }
        }
    }
    return false;
}

bool evaluate_branch(const Instruction& instr) {
    (void)instr;
    return true; 
}

vvoid simulate_pipeline() {
    if (ENABLE_REORDERING) {
        reorder_instructions();
    }
    
    vector<int> IF(MAX_CYCLES, -1);
    vector<int> ID(MAX_CYCLES, -1);
    vector<int> EX(MAX_CYCLES, -1);
    vector<int> MEM(MAX_CYCLES, -1);
    vector<int> WB(MAX_CYCLES, -1);

    int next_fetch_idx = 0;
    total_cycles = 0;
    executed_instructions = 0;
    register_writer.clear();
    cycle_notes.clear();

    bool done = false;

    while (!done && total_cycles < MAX_CYCLES) {

        // Writeback Stage
        if (total_cycles > 0 && WB[total_cycles - 1] != -1) {
            Instruction &instr = instructions[WB[total_cycles - 1]];
            instr.wb_cycle = total_cycles - 1;
            instr.executed = true;
            executed_instructions++;
            
            // Clear register writer entry after writeback
            for (auto it = register_writer.begin(); it != register_writer.end();) {
                if (it->second == WB[total_cycles - 1]) {
                    it = register_writer.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Pipeline Shifting
        if (total_cycles > 0) {
            WB[total_cycles] = MEM[total_cycles - 1];
            MEM[total_cycles] = EX[total_cycles - 1];
            EX[total_cycles] = ID[total_cycles - 1];
            ID[total_cycles] = IF[total_cycles - 1];
        }

        bool stall = false;
        bool branch_misprediction = false;

        // Stall Logic - When both Forwarding and Reordering are OFF
        if (ID[total_cycles] != -1) {
            int ex_idx = EX[total_cycles];
            int mem_idx = MEM[total_cycles];
            int id_idx = ID[total_cycles];

            if (has_data_hazard(ex_idx, id_idx) || has_data_hazard(mem_idx, id_idx)) {
                if (!ENABLE_FORWARDING && !ENABLE_REORDERING) {
                    stall = true;
                    int stall_cycles = 2;  // Stall for 2 cycles
                    cycle_notes[total_cycles] = "STALL (Data Hazard, FWD & REORD OFF)";
                    
                    // Insert NOPs for stall duration
                    for (int s = 0; s < stall_cycles; ++s) {
                        IF[total_cycles + s] = -1;
                        ID[total_cycles + s] = -1;
                    }
                    
                    total_cycles += stall_cycles;  // Adjust cycle count for stalls
                } else {
                    stall = true;
                    cycle_notes[total_cycles] = "STALL (Data Hazard)";
                    IF[total_cycles] = -1;
                }
            }
        }

        // Branch Misprediction Handling
        if (EX[total_cycles] != -1 && is_branch(instructions[EX[total_cycles]].name)) {
            bool actual_taken = evaluate_branch(instructions[EX[total_cycles]]);
            branch_misprediction = (actual_taken != instructions[EX[total_cycles]].taken);

            if (branch_misprediction) {
                IF[total_cycles] = -1;
                ID[total_cycles] = -1;
                next_fetch_idx = actual_taken ? EX[total_cycles] + 1 : next_fetch_idx;
                cycle_notes[total_cycles] = "FLUSH (Branch Misprediction)";
            }
        }

        // Fetch Stage
        if (!stall && !branch_misprediction && next_fetch_idx < (int)instructions.size()) {
            IF[total_cycles] = next_fetch_idx;
            instructions[next_fetch_idx].fetch_cycle = total_cycles;
            next_fetch_idx++;
        }

        // Update Cycle Information for other stages
        if (ID[total_cycles] != -1)
            instructions[ID[total_cycles]].decode_cycle = total_cycles;
        if (EX[total_cycles] != -1) {
            instructions[EX[total_cycles]].execute_cycle = total_cycles;
            if (!instructions[EX[total_cycles]].dest_reg.empty()) {
                register_writer[instructions[EX[total_cycles]].dest_reg] = EX[total_cycles];
            }
        }
        if (MEM[total_cycles] != -1) {
            instructions[MEM[total_cycles]].mem_cycle = total_cycles;
        }

        // Check for completion
        if (executed_instructions == (int)instructions.size()) {
            done = true;
        }

        total_cycles++;
    }
}



void display_pipeline_chart() {
    int display_cycles = min(total_cycles, DISPLAY_CYCLES);
    total_cycles = total_cycles-1;
    cout << "\n" << CLR_BOLD << CLR_CYAN << "MIPS Pipeline Simulation" << CLR_RESET << "\n";
    cout << "Instructions: " << instructions.size() << " | Cycles: " << total_cycles;
    if (total_cycles > DISPLAY_CYCLES) {
        cout << " (showing first " << DISPLAY_CYCLES << ")";
    }
    cout << "\n\n";
    
    cout << setw(20) << left << "Instruction";
    for (int c = 0; c < display_cycles; ++c) {
        cout << setw(4) << c;
    }
    cout << "\n" << string(20 + 4 * display_cycles, '-') << "\n";

    for (size_t i = 0; i < instructions.size(); ++i) {
        cout << setw(20) << left << instructions[i].original.substr(0, 18);
        
        for (int c = 0; c < display_cycles; ++c) {
            string stage = "";
            
            if (c == instructions[i].fetch_cycle) stage = "IF";
            else if (c == instructions[i].decode_cycle) stage = "ID";
            else if (c == instructions[i].execute_cycle) stage = "EX";
            else if (c == instructions[i].mem_cycle) stage = "MEM";
            else if (c == instructions[i].wb_cycle) stage = "WB";
            
            if (stage == "IF") cout << CLR_BLUE << setw(4) << "IF" << CLR_RESET;
            else if (stage == "ID") cout << CLR_YELLOW << setw(4) << "ID" << CLR_RESET;
            else if (stage == "EX") cout << CLR_MAGENTA << setw(4) << "EX" << CLR_RESET;
            else if (stage == "MEM") cout << CLR_CYAN << setw(4) << "MEM" << CLR_RESET;
            else if (stage == "WB") cout << CLR_GREEN << setw(4) << "WB" << CLR_RESET;
            else cout << setw(4) << " ";
        }
        cout << "\n";
    }
    
    cout << setw(20) << left << "EVENTS";
    for (int c = 0; c < display_cycles; ++c) {
        if (cycle_notes.find(c) != cycle_notes.end()) {
            cout << CLR_RED << setw(4) << "!" << CLR_RESET;
        } else {
            cout << setw(4) << " ";
        }
    }
    cout << "\n\n";
    cout << "\n" << CLR_BOLD << "Statistics:" << CLR_RESET << "\n";
    cout << "Total Cycles: " << total_cycles << "\n";
    cout << "Executed Instructions: " << executed_instructions << "\n";
    cout << "CPI: " << fixed << setprecision(2) 
         << (double)total_cycles / executed_instructions << "\n";
    cout << "Throughput (instr/cycle): " << fixed << setprecision(3) 
         << (double)executed_instructions / total_cycles << "\n";
    cout << "Total Time (ps): " << total_cycles * 200 << "\n";
    cout << "\n" << CLR_BOLD << "Pipeline Events:" << CLR_RESET << "\n";
    for (const auto& note : cycle_notes) {
        cout << "Cycle " << note.first << ": " << note.second << "\n";
    }
}

void export_to_excel() {
    lxw_workbook *workbook = workbook_new("pipeline_report.xlsx");
    if (!workbook) {
        cerr << CLR_RED << "Failed to create Excel workbook!" << CLR_RESET << endl;
        return;
    }

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, "Pipeline");
    if (!worksheet) {
        cerr << CLR_RED << "Failed to create worksheet!" << CLR_RESET << endl;
        workbook_close(workbook);
        return;
    }

    lxw_format *header_format = workbook_add_format(workbook);
    format_set_bold(header_format);
    format_set_bg_color(header_format, LXW_COLOR_GRAY);
    lxw_format *if_format = workbook_add_format(workbook);
    format_set_bg_color(if_format, LXW_COLOR_BLUE);
    format_set_font_color(if_format, LXW_COLOR_WHITE);
    lxw_format *id_format = workbook_add_format(workbook);
    format_set_bg_color(id_format, LXW_COLOR_YELLOW);
    lxw_format *ex_format = workbook_add_format(workbook);
    format_set_bg_color(ex_format, LXW_COLOR_MAGENTA);
    format_set_font_color(ex_format, LXW_COLOR_WHITE);
    lxw_format *mem_format = workbook_add_format(workbook);
    format_set_bg_color(mem_format, LXW_COLOR_CYAN);
    lxw_format *wb_format = workbook_add_format(workbook);
    format_set_bg_color(wb_format, LXW_COLOR_GREEN);

    lxw_format *event_format = workbook_add_format(workbook);
    format_set_bg_color(event_format, LXW_COLOR_RED);
    format_set_font_color(event_format, LXW_COLOR_WHITE);

    int export_cycles = total_cycles;

    worksheet_write_string(worksheet, 0, 0, "Instruction", header_format);
    for (int c = 0; c < export_cycles; ++c) {
        worksheet_write_string(worksheet, 0, c + 1, ("C" + to_string(c)).c_str(), header_format);
    }

    for (size_t i = 0; i < instructions.size(); ++i) {
        worksheet_write_string(worksheet, i + 1, 0, instructions[i].original.c_str(), NULL);

        for (int c = 0; c < export_cycles; ++c) {
            lxw_format *cell_format = NULL;
            const char* stage = "";

            if (c == instructions[i].fetch_cycle) {
                stage = "IF";
                cell_format = if_format;
            }
            else if (c == instructions[i].decode_cycle) {
                stage = "ID";
                cell_format = id_format;
            }
            else if (c == instructions[i].execute_cycle) {
                stage = "EX";
                cell_format = ex_format;
            }
            else if (c == instructions[i].mem_cycle) {
                stage = "MEM";
                cell_format = mem_format;
            }
            else if (c == instructions[i].wb_cycle) {
                stage = "WB";
                cell_format = wb_format;
            }

            if (cell_format) {
                worksheet_write_string(worksheet, i + 1, c + 1, stage, cell_format);
            }
        }
    }

    int events_row = instructions.size() + 1;
    worksheet_write_string(worksheet, events_row, 0, "EVENTS", NULL);

    for (int c = 0; c < export_cycles; ++c) {
        if (cycle_notes.find(c) != cycle_notes.end()) {
            worksheet_write_string(worksheet, events_row, c + 1, "!", event_format);
        }
    }

    int stats_row = instructions.size() + 3;
    worksheet_write_string(worksheet, stats_row, 0, "Total Cycles", NULL);
    worksheet_write_number(worksheet, stats_row, 1, export_cycles, NULL);
    worksheet_write_string(worksheet, stats_row + 1, 0, "Executed Instructions", NULL);
    worksheet_write_number(worksheet, stats_row + 1, 1, executed_instructions, NULL);
    worksheet_write_string(worksheet, stats_row + 2, 0, "CPI", NULL);
    worksheet_write_number(worksheet, stats_row + 2, 1, (double)export_cycles / executed_instructions, NULL);
    worksheet_write_string(worksheet, stats_row + 3, 0, "Throughput (instr/cycle)", NULL);
    worksheet_write_number(worksheet, stats_row + 3, 1, (double)executed_instructions / export_cycles, NULL);
    worksheet_write_string(worksheet, stats_row + 4, 0, "Total Time (ps)", NULL);
    worksheet_write_number(worksheet, stats_row + 4, 1, export_cycles * CYCLE_TIME_PS, NULL);

    workbook_close(workbook);
}


int main() {
    cout<<endl<<endl;
    cout << CLR_BOLD << "MIPS 5-Stage Pipeline Simulator" << CLR_RESET << "\n";
    cout << "Branch Prediction: " << (ENABLE_BRANCH_PREDICTION ? "ON" : "OFF") << "\n";
    cout << "Forwarding: " << (ENABLE_FORWARDING ? "ON" : "OFF") << "\n";
    cout << "Reordering: " << (ENABLE_REORDERING ? "ON" : "OFF") << "\n\n";

    try {
        load_program("test.asm");
        simulate_pipeline();
        display_pipeline_chart();
        export_to_excel();
        cout << CLR_GREEN << "\nSimulation completed successfully!" << CLR_RESET << endl;
        cout << "Excel report saved to pipeline_report.xlsx" << endl<<endl<<endl;
        return 0;
        
    } 
    catch (const exception& e) {
        cerr << CLR_RED << "Error: " << e.what() << CLR_RESET << endl;
        return 1;
    }

}