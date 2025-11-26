#!/usr/bin/env python3
"""Precompute tokens for conversation prompts"""

import sys
import tiktoken
import json

def main():
    if len(sys.argv) < 3:
        print("Usage: python precompute_tokens.py <input_file> <output_file>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    # Load encoding
    enc = tiktoken.get_encoding("cl100k_base")
    
    # Read conversation file
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    result = {
        "prompts": []
    }
    
    # Process each prompt
    for line in lines:
        line = line.strip()
        if line:
            tokens = enc.encode(line)
            result["prompts"].append({
                "text": line,
                "tokens": tokens,
                "token_count": len(tokens)
            })
    
    # Write output
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2)
    
    print(f"✓ Precomputed tokens for {len(result['prompts'])} prompts")
    print(f"  Output: {output_file}")

if __name__ == "__main__":
    main()
