
import sys

path = r'c:\Users\elias\Desktop\GITHUB\epyks\epyks\epyks.Server\main.cpp'
with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
skip = 0
for i, line in enumerate(lines):
    new_lines.append(line)
    if 'auto pendingRequests = db->GetPendingFriendRequests(req.username);' in line:
        # Find the end of the for loop
        j = i + 1
        while j < len(lines) and 'SendTo(sock, notify);' not in lines[j]:
            j += 1
        while j < len(lines) and '}' not in lines[j]:
            j += 1
        
        if j < len(lines):
            indent = lines[j].split('}')[0]
            dm_logic = [
                f"{indent}  // Send DM contacts\n",
                f"{indent}  {{\n",
                f"{indent}    auto contacts = db->GetDMContacts(req.username);\n",
                f"{indent}    std::string dmPayload;\n",
                f"{indent}    for (auto &c : contacts) dmPayload += c + \",\";\n",
                f"{indent}    epyks::Packet dmsPkt;\n",
                f"{indent}    dmsPkt.type = epyks::PacketType::MY_DMS;\n",
                f"{indent}    dmsPkt.data = dmPayload;\n",
                f"{indent}    SendTo(sock, dmsPkt);\n",
                f"{indent}  }}\n"
            ]
            # We will append this once we reach the closing brace in the main loop
            # Or just append it right after the loop in our new_lines?
            # Let's just insert it now.
            # But wait, the main loop is still iterating.
            # I'll use a different approach.

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# Pattern to match
target = """            // Send pending friend requests to the newly logged-in user
            auto pendingRequests = db->GetPendingFriendRequests(req.username);
            for (const auto& sender : pendingRequests) {
              epyks::Packet notify;
              notify.type = epyks::PacketType::FRIEND_REQUEST;
              notify.data = sender + " wants to add you as friend";
              SendTo(sock, notify);
            }"""

replacement = target + """
            // Send DM contacts
            {
              auto contacts = db->GetDMContacts(req.username);
              std::string dmPayload;
              for (auto &c : contacts) dmPayload += c + ",";
              epyks::Packet dmsPkt;
              dmsPkt.type = epyks::PacketType::MY_DMS;
              dmsPkt.data = dmPayload;
              SendTo(sock, dmsPkt);
            }"""

# Use replace with a count of 2 to catch both LOGIN and TOKEN_LOGIN
# We need to be careful with indentation.
# I'll just use a more flexible search.

content = content.replace(target, replacement)

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
print("Patch applied successfully")
