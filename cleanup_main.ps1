# Read the original file
$content = Get-Content "C:\Users\kraman\source\KVStoreV2\KVPlayground\src\main.cpp" -Raw

# Remove HttpTransportProtocol enum usage from includes section (if any)
# Note: We keep the include because KVTypes.h might still define it

# Remove static HttpTransportProtocol transportProtocol; from KVStoreManager
$content = $content -replace '(?m)^\s*static HttpTransportProtocol transportProtocol;.*$\r?\n', ''

# Remove transportProtocol parameter from GetInstance signature
$content = $content -replace 'HttpTransportProtocol transport\s*=\s*HttpTransportProtocol::WinHTTP,?\s*', ''

# Remove enableSdkLogging parameter from GetInstance signature
$content = $content -replace 'bool enableSdkLogging\s*=\s*false,?\s*', ''

# Remove enableMultiNic parameter from GetInstance signature
$content = $content -replace 'bool enableMultiNic\s*=\s*false,?\s*', ''

# Fix any trailing commas that might be left
$content = $content -replace ',\s*\)', ')'

# Remove transportProtocol = transport; assignment
$content = $content -replace '(?m)^\s*transportProtocol\s*=\s*transport;.*$\r?\n', ''

# Remove transport/multi-nic parameters from Initialize call
$content = $content -replace '(Initialize\([^)]*),\s*transport,\s*enableSdkLogging,\s*enableMultiNic', '$1'

# Remove HTTP Transport output line
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"\[KVStore\] Transport:.*$\r?\n', ''

# Remove transportProtocol static initialization
$content = $content -replace '(?m)^\s*HttpTransportProtocol KVStoreManager::transportProtocol.*$\r?\n', ''

# Remove transportProtocol variable declaration in main
$content = $content -replace '(?m)^\s*HttpTransportProtocol transportProtocol\s*=\s*HttpTransportProtocol::WinHTTP;.*$\r?\n', ''

# Remove enableSdkLogging and enableMultiNic variable declarations
$content = $content -replace '(?m)^\s*bool enableSdkLogging\s*=\s*false;.*$\r?\n', ''
$content = $content -replace '(?m)^\s*bool enableMultiNic\s*=\s*false;.*$\r?\n', ''

# Remove --transport argument parsing block
$content = $content -replace '(?s)\} else if \(\(arg == "--transport"[^}]+?\s+\}', '}'

# Remove --sdk-logging argument parsing
$content = $content -replace '(?m)^\s*\} else if \(arg == "--sdk-logging"[^\n]+\r?\n\s*enableSdkLogging = true;\r?\n', ''

# Remove --multi-nic argument parsing
$content = $content -replace '(?m)^\s*\} else if \(arg == "--multi-nic"[^\n]+\r?\n\s*enableMultiNic = true;\r?\n', ''

# Remove transport checks from positional argument validation
$content = $content -replace 'tokensFile == "--transport" \|\| tokensFile == "-t" \|\|\s*', ''
$content = $content -replace 'arg2 != "--transport" && arg2 != "-t" &&\s*', ''
$content = $content -replace 'arg3 != "--transport" && arg3 != "-t" &&\s*', ''

# Remove HTTP Transport output in main
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"HTTP Transport:.*$\r?\n', ''

# Remove transport help text
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  --transport, -t.*$\r?\n', ''
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  --sdk-logging.*$\r?\n', ''
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  --multi-nic.*$\r?\n', ''

# Remove transport example
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  " << argv\[0\] << " conversation_tokens\.json 5 2 --transport libcurl.*$\r?\n', ''

# Remove Transport Options section
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"\\nTransport Options:.*$\r?\n', ''
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  winhttp.*$\r?\n', ''
$content = $content -replace '(?m)^\s*std::cout\s*<<\s*"  libcurl.*$\r?\n', ''

# Remove transport variable copies in lambda
$content = $content -replace '(?m)^\s*HttpTransportProtocol transport\s*=\s*transportProtocol;.*$\r?\n', ''
$content = $content -replace '(?m)^\s*bool sdkLogging\s*=\s*enableSdkLogging;.*$\r?\n', ''
$content = $content -replace '(?m)^\s*bool multiNic\s*=\s*enableMultiNic;.*$\r?\n', ''

# Remove transport/sdkLogging/multiNic from lambda captures and parameters
$content = $content -replace '\[prompts, currentRunId, storage, container, level, transport, sdkLogging, multiNic\]', '[prompts, currentRunId, storage, container, level]'

# Remove transport/sdkLogging/multiNic from runConversation calls
$content = $content -replace '(runConversation\([^)]*),\s*transport,\s*sdkLogging,\s*multiNic', '$1'

# Write the cleaned content
$content | Set-Content "C:\Users\kraman\source\KVStoreV2\KVPlayground\src\main.cpp" -NoNewline

Write-Host "Cleanup complete! Transport-related code removed from main.cpp" -ForegroundColor Green
