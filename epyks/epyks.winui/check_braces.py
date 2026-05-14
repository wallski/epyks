import sys

def check_braces(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # Find where the namespace starts
    ns_start = content.find("namespace winrt::epyks_winui::implementation")
    if ns_start == -1:
        print("Namespace not found")
        return

    # Find the opening brace of the namespace
    brace_start = content.find("{", ns_start)
    if brace_start == -1:
        print("Opening brace not found")
        return

    # Count braces from brace_start to end
    sub_content = content[brace_start:]
    
    # We want to ignore braces inside strings and comments
    level = 0
    in_string = False
    in_char = False
    in_line_comment = False
    in_block_comment = False
    
    for i, char in enumerate(sub_content):
        if in_line_comment:
            if char == '\n': in_line_comment = False
            continue
        if in_block_comment:
            if sub_content[i:i+2] == '*/': in_block_comment = False
            continue
        if in_string:
            if char == '"' and sub_content[i-1] != '\\': in_string = False
            continue
        if in_char:
            if char == "'" and sub_content[i-1] != '\\': in_char = False
            continue
            
        if sub_content[i:i+2] == '//': in_line_comment = True
        elif sub_content[i:i+2] == '/*': in_block_comment = True
        elif char == '"': in_string = True
        elif char == "'": in_char = True
        elif char == '{': level += 1
        elif char == '}': 
            level -= 1
            if level == 0:
                print(f"Namespace closed at index {brace_start + i}")
                print(f"Remaining content length: {len(sub_content) - i}")
                print(f"Snippet of remaining: {sub_content[i:i+50]!r}")
                
    if level != 0:
        print(f"Final level: {level} (mismatch!)")
    else:
        print("Braces match perfectly.")

if __name__ == "__main__":
    check_braces(sys.argv[1])
