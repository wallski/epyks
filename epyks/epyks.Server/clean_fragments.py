import sys
import re

def clean_fragments(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        stripped = line.strip()
        # Fragments usually look like:
        # 1. Starts with " or +
        # 2. Ends with ); or )
        # 3. Contains lots of + 
        # 4. Is NOT a valid C++ statement (doesn't start with keyword, type, or known variable)
        
        # Specific pattern for the gui->AddLog residuals:
        # e.g. '"] requesting rename for server [" +'
        if (stripped.startswith('"') or stripped.startswith('+')) and (stripped.endswith(');') or stripped.endswith(')')):
            print(f"Removing fragment: {stripped}")
            continue
        
        # Also lines that are just indentation and then a string fragment
        if re.match(r'^\s*".*"\s*\);?\s*$', line) or re.match(r'^\s*".*"\s*$', line):
             # Wait, some legitimate lines might match this if they are just a string.
             # But in this file, most are fragments.
             pass

        # Let's be safer: if it contains std::to_string but no assignment or call
        if 'std::to_string' in stripped and not any(k in stripped for k in ['=', '(', 'printf', 'LogToFile', 'cout', 'return', 'if', 'while']):
             # Wait, if it has std::to_string and is a fragment
             print(f"Removing suspected fragment: {stripped}")
             continue

        new_lines.append(line)

    with open(filepath, 'w') as f:
        f.writelines(new_lines)

if __name__ == "__main__":
    clean_fragments(sys.argv[1])
