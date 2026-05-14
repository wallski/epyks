import sys

def check_braces(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    stack = []
    line_no = 1
    col_no = 1
    
    # We only care about parentheses here for the specific error
    for char in content:
        if char == '(':
            stack.append((line_no, col_no))
        elif char == ')':
            if not stack:
                print(f"Error: Unmatched ')' at line {line_no}, col {col_no}")
            else:
                stack.pop()
        
        if char == '\n':
            line_no += 1
            col_no = 1
        else:
            col_no += 1

    if stack:
        for l, c in stack:
            print(f"Error: Unmatched '(' started at line {l}, col {c}")

if __name__ == "__main__":
    check_braces(sys.argv[1])
