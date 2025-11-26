#!/usr/bin/env python3
"""
GPT-4 Tokenizer using OpenAI's tiktoken library
Provides accurate cl100k_base encoding for GPT-4 and GPT-3.5-turbo
"""

import sys
import tiktoken
import json

def tokenize_text(text, encoding_name="cl100k_base"):
    """
    Tokenize text using the specified encoding.
    
    Args:
        text: Input text to tokenize
        encoding_name: Encoding to use (default: cl100k_base for GPT-4)
    
    Returns:
        Dictionary with token information
    """
    try:
        encoding = tiktoken.get_encoding(encoding_name)
        tokens = encoding.encode(text)
        
        # Decode each token to show what it represents
        token_strings = [encoding.decode([token]) for token in tokens]
        
        return {
            "text": text,
            "encoding": encoding_name,
            "token_count": len(tokens),
            "tokens": tokens,
            "token_strings": token_strings,
            "char_count": len(text),
            "chars_per_token": len(text) / len(tokens) if tokens else 0
        }
    except Exception as e:
        return {
            "error": str(e)
        }

def interactive_mode():
    """Run in interactive mode for chatbot-like experience"""
    print("=== GPT-4 Tokenizer (Python + tiktoken) ===")
    print("Using cl100k_base encoding (GPT-4, GPT-3.5-turbo)")
    print("Type your prompts to see accurate tokenization!")
    print("Type 'exit' or 'quit' to end the session.")
    print("============================================\n")
    
    encoding = tiktoken.get_encoding("cl100k_base")
    
    # Show examples
    print("Examples:")
    examples = [
        "Hello, world!",
        "GPT-4 is amazing",
        "The quick brown fox jumps"
    ]
    
    for example in examples:
        tokens = encoding.encode(example)
        print(f'  "{example}" -> {len(tokens)} tokens')
    print()
    
    while True:
        try:
            user_input = input("\nYou: ")
            
            if user_input.lower() in ['exit', 'quit', 'q']:
                print("\nGoodbye!")
                break
            
            if not user_input:
                continue
            
            # Tokenize
            tokens = encoding.encode(user_input)
            token_strings = [encoding.decode([token]) for token in tokens]
            
            # Display results
            print(f"\n--- Tokenization Results ---")
            print(f"Token count: {len(tokens)}")
            print(f"Characters: {len(user_input)}")
            print(f"Avg chars/token: {len(user_input)/len(tokens):.2f}\n")
            
            # Show token breakdown
            print(f"Tokens ({len(tokens)}): [", end="")
            for i, token_str in enumerate(token_strings[:20]):
                # Escape special characters for display
                display_str = repr(token_str)[1:-1]  # Remove outer quotes
                print(f'"{display_str}"', end="")
                if i < len(token_strings) - 1:
                    print(", ", end="")
            if len(token_strings) > 20:
                print(f" ... ({len(token_strings) - 20} more)", end="")
            print("]\n")
            
            # Show token IDs
            print(f"Token IDs: {tokens[:30]}", end="")
            if len(tokens) > 30:
                print(f" ... ({len(tokens) - 30} more)")
            else:
                print()
            
        except KeyboardInterrupt:
            print("\n\nGoodbye!")
            break
        except Exception as e:
            print(f"Error: {e}")

def main():
    """Main entry point"""
    if len(sys.argv) == 1:
        # No arguments - run interactive mode
        interactive_mode()
    elif sys.argv[1] == "--json":
        # JSON mode for programmatic use
        if len(sys.argv) < 3:
            print(json.dumps({"error": "No text provided"}))
            sys.exit(1)
        
        # Check if reading from stdin
        if sys.argv[2] == "-":
            text = sys.stdin.read()
        else:
            text = sys.argv[2]
            
        result = tokenize_text(text)
        print(json.dumps(result, indent=2))
    elif sys.argv[1] == "--count":
        # Just return token count
        if len(sys.argv) < 3:
            print("0")
            sys.exit(1)
        
        text = sys.argv[2]
        encoding = tiktoken.get_encoding("cl100k_base")
        tokens = encoding.encode(text)
        print(len(tokens))
    else:
        # Single text argument - tokenize and display
        text = sys.argv[1]
        result = tokenize_text(text)
        
        if "error" in result:
            print(f"Error: {result['error']}")
            sys.exit(1)
        
        print(f"Text: {result['text']}")
        print(f"Tokens: {result['token_count']}")
        print(f"Token IDs: {result['tokens']}")
        print(f"Characters: {result['char_count']}")
        print(f"Chars/Token: {result['chars_per_token']:.2f}")

if __name__ == "__main__":
    main()
