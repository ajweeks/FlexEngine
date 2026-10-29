#!/usr/bin/env python3
"""PreToolUse(Bash) hook: subagents must build & run only through scripts/flexsvc (queued builds, sandboxes, GPU
slots), so block direct MSBuild, ninja & Flex.exe calls from them. The main session (the user's or the manager's) is
not affected: hook input only has agent_id inside a subagent."""

import json, re, shlex, sys

BLOCKED = {"msbuild", "msbuild.exe", "ninja", "flex.exe"}
WRAPPERS = {"cmd.exe", "cmd", "powershell.exe", "powershell", "pwsh", "pwsh.exe", "start"}
PREFIXES = {"sudo", "nice", "nohup", "env", "time", "exec", "timeout", "stdbuf"}


def first_word(tokens):
	"""The program a segment runs, skipping env assignments & prefixes like `nice -n 10` / `timeout 60`."""
	i = 0
	while i < len(tokens):
		t = tokens[i]
		if re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", t) or t.startswith("-") or t.isdigit():
			i += 1
		elif t.rsplit("/", 1)[-1] in PREFIXES:
			i += 1
		else:
			return t, tokens[i + 1:]
	return None, []


def blocked(command):
	for seg in re.split(r"&&|\|\||[;|&\n]|\$\(|`", command):
		try:
			tokens = shlex.split(seg)
		except ValueError:
			tokens = seg.split()
		prog, rest = first_word(tokens)
		if not prog:
			continue
		name = re.split(r"[/\\]", prog)[-1].lower()
		if name in BLOCKED:
			return name
		if name in WRAPPERS:  # cmd.exe /c "...\Flex.exe" etc.
			for t in rest:
				for word in t.split():
					w = re.split(r"[/\\]", word.strip("\"'"))[-1].lower()
					if w in BLOCKED:
						return w
	return None


def main():
	data = json.load(sys.stdin)
	if not data.get("agent_id") or data.get("tool_name") != "Bash":
		return
	hit = blocked(data.get("tool_input", {}).get("command", ""))
	if hit:
		print("Blocked: subagents don't run %s directly. Use `scripts/flexsvc -a <name> build` (or `build --linux`/"
			"`build --mine`) & `scripts/flexsvc -a <name> run -- <args>`; see docs/agents.md." % hit, file=sys.stderr)
		sys.exit(2)


if __name__ == "__main__":
	main()
