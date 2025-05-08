import sys

opcode_map = {
    "000000": "R-type",
    "100011": "lw",
    "101011": "sw",
    "000100": "beq",
    "000101": "bne",
    "000010": "j",
    "000011": "jal"
}

funct_map = {
    "100000": "add",
    "100010": "sub",
    "100100": "and",
    "100101": "or",
    "101010": "slt"
}

register_map = {
    "00000": "$zero", "00001": "$at", "00010": "$v0", "00011": "$v1",
    "00100": "$a0", "00101": "$a1", "00110": "$a2", "00111": "$a3",
    "01000": "$t0", "01001": "$t1", "01010": "$t2", "01011": "$t3",
    "01100": "$t4", "01101": "$t5", "01110": "$t6", "01111": "$t7"
}

def parse_r_type(binary):
    rs = register_map[binary[6:11]]
    rt = register_map[binary[11:16]]
    rd = register_map[binary[16:21]]
    funct = binary[26:]

    if funct in funct_map:
        return f"{funct_map[funct]} {rd}, {rs}, {rt}"
    else:
        return f"# Unknown R-type funct: {funct}"

def parse_i_type(binary, opcode):
    rs = register_map[binary[6:11]]
    rt = register_map[binary[11:16]]
    immediate = int(binary[16:], 2)

    if opcode == "100011":  # lw
        return f"lw {rt}, {immediate}({rs})"
    elif opcode == "101011":  # sw
        return f"sw {rt}, {immediate}({rs})"
    elif opcode == "000100":  # beq
        return f"beq {rs}, {rt}, {immediate}"
    elif opcode == "000101":  # bne
        return f"bne {rs}, {rt}, {immediate}"
    else:
        return f"# Unknown I-type opcode: {opcode}"

def parse_j_type(binary):
    address = int(binary[6:], 2) << 2
    return f"j {address}"

def opcode_to_asm(line):

    clean_line = line.split('#')[0].strip()

    if not clean_line:
        return ""  


    if len(clean_line) != 32:
        return f"# Invalid opcode length: {clean_line}"

    opcode = clean_line[0:6]

    if opcode == "000000":
        return parse_r_type(clean_line)
    elif opcode in ["000010", "000011"]:
        return parse_j_type(clean_line)
    elif opcode in opcode_map:
        return parse_i_type(clean_line, opcode)
    else:
        return f"# Unknown opcode: {opcode}"

def main():
    try:
        with open("opcode.txt", "r") as infile:
            lines = infile.readlines()

        asm_lines = [opcode_to_asm(line.strip()) for line in lines]

        with open("test.asm", "w") as outfile:
            outfile.write("\n".join(asm_lines))

        print("Conversion complete. Check 'test.asm' for the generated assembly.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
