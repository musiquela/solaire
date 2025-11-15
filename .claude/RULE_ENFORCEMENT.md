# RULE ENFORCEMENT PROTOCOL

## RULE #0: NO AI ATTRIBUTION (HIGHEST PRIORITY)

**NEVER mention "Claude", "Claude Code", or any AI attribution in:**
- Git commit messages
- Code comments
- Documentation files
- Any project files

**FORBIDDEN PHRASES:**
- ❌ "Generated with Claude Code"
- ❌ "Co-Authored-By: Claude"
- ❌ Any reference to AI assistance

**This is NON-NEGOTIABLE and supersedes all other rules.**

---

## MANDATORY CHECK BEFORE ANY CODE

Before writing ANY code that implements audio processing, DSP, or JUCE functionality, I MUST:

### 1. STOP AND ASK
```
Am I about to write custom code, or am I copying from a verified JUCE example?

If "custom code" → STOP
If "from example" → Proceed with citation
```

### 2. VERIFY SOURCES
For EVERY function/algorithm, answer:
- [ ] Do I have a direct JUCE example of this exact thing?
- [ ] Can I cite the source URL/file?
- [ ] Have I seen this pattern in 2+ places?

If ANY answer is "no" → STOP and search for examples

### 3. EXPLICIT PERMISSION REQUIRED
I am **FORBIDDEN** from implementing code without JUCE examples UNLESS you say:

**"Override rules for [specific function]"**

Without this explicit override, I MUST:
1. Stop
2. Tell you I don't have examples
3. Search for examples
4. Only proceed if examples found

## ENFORCEMENT CHECKLIST

Copy this into EVERY code response:

```
✓ Rule #1 Check: Using multi-point JUCE examples? [YES/NO + citations]
✓ Rule #2 Check: Am I 95% certain? [YES/NO + reasoning]
✓ Rule #3 Check: Have I verified against real JUCE code? [YES/NO + sources]
✓ Rule #4 Check: Can I debug this myself? [YES/NO]
✓ Rule #5 Check: Am I 95% certain user can test this? [YES/NO]
```

If ANY checkbox is "NO" → STOP and ask for guidance

## AUTOPILOT PREVENTION

### BANNED PHRASES (indicate rule-breaking):
- ❌ "This should work..."
- ❌ "In theory..."
- ❌ "A reasonable approach would be..."
- ❌ "Let me implement..."
- ❌ "Based on DSP principles..."
- ❌ "We can design..."

### REQUIRED PHRASES (indicate rule-following):
- ✅ "From audiodev.blog pattern..."
- ✅ "According to JUCE forum thread..."
- ✅ "Copying from JUCE example at..."
- ✅ "I don't have a verified example of this, let me search..."
- ✅ "I'm not 95% certain, stopping to research..."

## OVERRIDE PROTOCOL

If you want me to implement something WITHOUT examples, you must say:

**"Override Rule #1 for [function name]"**

Then I will:
1. Acknowledge the override
2. Mark the code with `// RULE OVERRIDE: No JUCE example`
3. Warn about potential issues
4. Proceed with theoretical implementation

## SESSION START REMINDER

Every `/start` must include this check:
```
Before ANY code:
1. Read .claude/instructions.md
2. Read .claude/RULE_ENFORCEMENT.md
3. Check .claude/NEXT_STEPS.md for context
4. Run 95% certainty self-assessment
5. ONLY proceed if all rules satisfied
```

## VIOLATION DETECTION

If you see me:
- Writing code without citing sources
- Saying "this should work" without examples
- Implementing DSP from theory
- Proceeding without 95% certainty

**Immediately tell me:**
"You're breaking the rules. Stop and search for examples."

## HONESTY REQUIREMENT

If I don't have examples, I MUST say:
"I don't have verified JUCE examples for [X]. I need to either:
1. Search for examples now
2. Get permission to override rules
3. Simplify to only what I have examples for"

I am **FORBIDDEN** from pretending I have examples when I don't.

## THIS IS NON-NEGOTIABLE

These rules exist because my autopilot creates unverified code that wastes your time.

The enforcement mechanism is:
**STOP → CITE → VERIFY → PROCEED**

Never:
**THINK → IMPLEMENT → HOPE IT WORKS**
