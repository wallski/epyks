import sys
import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # 1. Remove if (gui) { ... } blocks
    # We use a non-greedy match for the block, but braces can be nested.
    # A simple regex won't handle nested braces.
    # Let's use a smarter approach.
    
    lines = content.splitlines()
    new_lines = []
    skip_depth = 0
    
    for line in lines:
        stripped = line.strip()
        
        if skip_depth > 0:
            skip_depth += line.count('{')
            skip_depth -= line.count('}')
            if skip_depth < 0: skip_depth = 0
            continue

        if stripped.startswith('if (gui)') or stripped.startswith('else if (gui)'):
            if '{' in line:
                skip_depth = line.count('{') - line.count('}')
                if skip_depth < 0: skip_depth = 0
            else:
                # One-liner - skip next line?
                # In this file, they usually have braces or are followed by exactly one gui-> call.
                # If it's a one-liner like "if (gui) gui->AddLog(...);", we can regex it.
                pass
            continue
            
        new_lines.append(line)

    content = "\n".join(new_lines)
    
    # 2. Remove any remaining single-line if (gui) and gui-> calls
    content = re.sub(r'if\s*\(gui\)\s*gui->[^\n]+', '', content)
    content = re.sub(r'gui->[^\n]+', '', content)
    
    # 3. Remove gui member from ChatServer class
    content = re.sub(r'ServerGUI\s*\*gui\s*=\s*nullptr;', '', content)
    content = re.sub(r'void\s+SetGUI\(ServerGUI\s*\*g\)\s*\{\s*gui\s*=\s*g;\s*\}', '', content)

    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    patch_file(sys.argv[1])
