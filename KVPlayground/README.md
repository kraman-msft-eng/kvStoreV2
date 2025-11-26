# KVPlayground - GPT-4 Tokenizer

Interactive tool for GPT-4 compatible tokenization with both C++ estimation and Python accuracy.

## Overview

This playground provides **two ways** to tokenize text for GPT-4:

1. **Python (Accurate)**: Uses OpenAI's official `tiktoken` library for exact cl100k_base tokenization
2. **C++ (Estimated)**: Fast approximation based on GPT-4 patterns, with optional Python validation

## Quick Start

### Option 1: Python (Recommended for Accuracy)

```powershell
# Install tiktoken
pip install tiktoken

# Run interactive tokenizer
python tokenizer.py
```

### Option 2: C++ with Python Validation

```powershell
# Build the project
.\build_native.ps1

# Run (automatically uses Python tiktoken if available)
.\build\KVPlayground\KVPlayground.exe
```

### Option 3: C++ Only (Estimation)

If Python/tiktoken is not installed, the C++ version provides pattern-based estimation.

## Python Script Usage

The `tokenizer.py` script can be used in multiple modes:

```powershell
# Interactive mode
python tokenizer.py

# Get token count only
python tokenizer.py --count "Your text here"

# Get full JSON output
python tokenizer.py --json "Your text here"

# Simple tokenization
python tokenizer.py "Hello, world!"
```

## Features

- **Accurate Tokenization**: Python version uses official OpenAI tiktoken (cl100k_base encoding)
- **Fast Estimation**: C++ version provides quick approximations
- **Hybrid Mode**: C++ app calls Python for validation when available
- **Interactive Interface**: Real-time tokenization as you type
- **Statistics**: Token count, character count, chars-per-token ratio
- **Comparison**: Shows estimated vs accurate token counts

## Example Output

```
You: Hello, world! This is a test of GPT-4 tokenization.

--- Token Estimation ---
Estimated tokens: 13
Accurate tokens:  13 (Python tiktoken)
Characters: 53
Avg chars/token: 4.08

Text breakdown:
Tokens (13): ["Hello", ",", " world", "!", " This", " is", " a", " test", " of", " GPT", "-", "4", " tokenization", "."]
```

## Integration with kvStore

This playground tool can be used to estimate token usage when storing prompts and completions in the kvStore key-value store, helping optimize storage costs for LLM applications.
