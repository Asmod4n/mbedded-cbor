#!/usr/bin/env python3
# Refuses a Bash command of a Claude session that sends stderr to /dev/null. Exit 2 refuses and gives the reason on
# stderr; exit 0 lets the command through. Any fault of this script lets the command through.
import json
import re
import sys


def stderr_to_null(cmd):
    if re.search(r'(^|[^0-9>])2>\s*/dev/null', cmd) or re.search(r'&>\s*/dev/null', cmd):
        return True
    return bool(re.search(r'(^|[^0-9&])>\s*/dev/null\s+2>&1', cmd))


def tool_trailer_in_commit(cmd):
    if not re.search(r'\bgit\b[^;&|\n]*\b(commit|commit-tree|tag)\b', cmd):
        return False
    return bool(re.search(r'Co-Authored-By:\s*Claude|Claude-Session:|claude\.ai/code|Generated with \[Claude Code\]', cmd, re.I))


RULES = [
    (tool_trailer_in_commit, 'Refused: the commit message points at a tool. Nothing points at a tool; remove the Co-Authored-By, Claude-Session and claude.ai lines.'),
    (stderr_to_null, 'Refused by .claude/hooks/guard_bash.py: stderr goes to /dev/null. Nothing sends stderr to '
                     '/dev/null; write it to a log file.'),
]


def main():
    try:
        cmd = json.load(sys.stdin)['tool_input']['command']
    except Exception as e:
        print('guard_bash: input not read (%s); the command passes' % e, file=sys.stderr)
        return 0
    for check, reason in RULES:
        try:
            if check(cmd):
                print(reason, file=sys.stderr)
                return 2
        except Exception as e:
            print('guard_bash: rule failed (%s); the command passes' % e, file=sys.stderr)
    return 0


sys.exit(main())
