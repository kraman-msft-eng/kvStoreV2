# Quick Start: Accurate GPT-4 Tokenization

## Using Python tiktoken (RECOMMENDED)

The Python version provides **accurate** GPT-4 tokenization using OpenAI's official tiktoken library.

### Setup
```powershell
pip install tiktoken
```

### Run Interactive Tokenizer
```powershell
cd KVPlayground
python tokenizer.py
```

### Example Session
```
=== GPT-4 Tokenizer (Python + tiktoken) ===
Using cl100k_base encoding (GPT-4, GPT-3.5-turbo)
Type your prompts to see accurate tokenization!
============================================

Examples:
  "Hello, world!" -> 4 tokens
  "GPT-4 is amazing" -> 6 tokens
  "The quick brown fox jumps" -> 5 tokens

You: Hello, world! This is a test of GPT-4 tokenization.

--- Tokenization Results ---
Token count: 16
Characters: 51
Avg chars/token: 3.19

Tokens (16): ["Hello", ",", " world", "!", " This", " is", " a", " test", " of", " G", "PT", "-", "4", " token", "ization", "."]

Token IDs: [9906, 11, 1917, 0, 1115, 374, 264, 1296, 315, 480, 2898, 12, 19, 4037, 2065, 13]
```

## Command Line Usage

```powershell
# Get just the token count
python tokenizer.py --count "Your text here"

# Get JSON output
python tokenizer.py --json "Your text here"

# Simple output
python tokenizer.py "Your text here"
```

## C++ Version

The C++ version provides fast estimation and can optionally validate with Python:

```powershell
.\build\KVPlayground\KVPlayground.exe
```

If Python tiktoken is installed, it will automatically show both estimated and accurate counts.
