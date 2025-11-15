---
description: Execute Session Start Protocol - loads rules and runs enforcement checks
---

## MANDATORY SESSION START PROTOCOL

### Step 1: Read ALL Rule Files
**MUST READ IN ORDER:**
1. `.claude/instructions.md` - The 5 strict rules
2. `.claude/RULE_ENFORCEMENT.md` - Enforcement protocol (NEW - CRITICAL)
3. `.claude/juce_critical_knowledge.md` - Verified JUCE patterns
4. `.claude/NEXT_STEPS.md` - Current project state

### Step 2: STOP and Self-Assess
Before taking ANY action on a JUCE task, you MUST answer these questions honestly:

**Certainty Check:**
1. Am I at 95% certainty about the threading model for this task?
2. Am I at 95% certainty about the build implications?
3. Am I at 95% certainty about the JUCE patterns I'll use?
4. Have I verified my approach against real production patterns?
5. **Do I have actual JUCE examples (not theory) for what I'm about to implement?**

**If ANY answer is "no":**
- STOP immediately
- Use MCP tools (firecrawl, context7) to research
- Reference knowledge files
- Do NOT proceed until 95% certain

### Step 3: BEFORE ANY CODE - Run Enforcement Check
```
✓ Rule #1: Using multi-point JUCE examples? [YES/NO + citations]
✓ Rule #2: Am I 95% certain? [YES/NO + reasoning]
✓ Rule #3: Verified against real JUCE code? [YES/NO + sources]
✓ Rule #4: Can I debug this myself? [YES/NO]
✓ Rule #5: 95% certain user can test this? [YES/NO]
```

**If ANY is "NO" → STOP and ask for guidance**
