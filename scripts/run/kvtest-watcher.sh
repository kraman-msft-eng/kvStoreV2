#!/bin/bash
# KV Offload Test Watcher
# This script runs on the Linux VM and watches a blob storage container for test commands
# When a command is found, it executes the test and uploads results back to blob storage

# Configuration
STORAGE_ACCOUNT="${STORAGE_ACCOUNT:-promptdatawestus2}"
COMMAND_CONTAINER="${COMMAND_CONTAINER:-kvtest-commands}"
RESULTS_CONTAINER="${RESULTS_CONTAINER:-kvtest-results}"
POLL_INTERVAL="${POLL_INTERVAL:-5}"  # seconds
KVPLAYGROUND_PATH="${KVPLAYGROUND_PATH:-~/kvgrpc/KVPlayground}"
CONVERSATION_JSON="${CONVERSATION_JSON:-~/kvgrpc/conversation_tokens.json}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if az CLI is installed
    if ! command -v az &> /dev/null; then
        log_error "Azure CLI not found. Please install: https://docs.microsoft.com/en-us/cli/azure/install-azure-cli"
        exit 1
    fi
    
    # Check if KVPlayground exists
    if [ ! -f "$KVPLAYGROUND_PATH" ]; then
        log_error "KVPlayground not found at: $KVPLAYGROUND_PATH"
        log_error "Set KVPLAYGROUND_PATH environment variable to the correct path"
        exit 1
    fi
    
    # Check if conversation_tokens.json exists
    if [ ! -f "$CONVERSATION_JSON" ]; then
        log_error "conversation_tokens.json not found at: $CONVERSATION_JSON"
        exit 1
    fi
    
    # Check if logged in to Azure
    if ! az account show &> /dev/null; then
        log_error "Not logged in to Azure. Please run: az login"
        exit 1
    fi
    
    log_success "All prerequisites met"
}

# Download command blob
get_pending_command() {
    # List blobs in command container (simplified query for compatibility)
    local blob_list=$(az storage blob list \
        --container-name "$COMMAND_CONTAINER" \
        --account-name "$STORAGE_ACCOUNT" \
        --auth-mode login \
        --query "[].name" \
        --output tsv 2>/dev/null | head -n 1)
    
    if [ -z "$blob_list" ]; then
        return 1
    fi
    
    echo "$blob_list"
    return 0
}

# Download and parse command
download_command() {
    local blob_name=$1
    local temp_file="/tmp/kvtest-command-$$.json"
    
    # Log to stderr so it doesn't get captured by command substitution
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Downloading command: $blob_name" >&2
    
    az storage blob download \
        --container-name "$COMMAND_CONTAINER" \
        --name "$blob_name" \
        --file "$temp_file" \
        --account-name "$STORAGE_ACCOUNT" \
        --auth-mode login \
        --output none 2>/dev/null
    
    if [ $? -eq 0 ] && [ -f "$temp_file" ]; then
        echo "$temp_file"
        return 0
    else
        echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Failed to download command blob" >&2
        return 1
    fi
}

# Delete command blob after processing
delete_command() {
    local blob_name=$1
    
    log_info "Deleting processed command: $blob_name"
    
    az storage blob delete \
        --container-name "$COMMAND_CONTAINER" \
        --name "$blob_name" \
        --account-name "$STORAGE_ACCOUNT" \
        --auth-mode login \
        --output none 2>/dev/null
}

# Execute KVPlayground test
execute_test() {
    local command_file=$1
    local job_id=$2
    
    # Parse command JSON
    local account_name=$(jq -r '.accountName' "$command_file")
    local container_name=$(jq -r '.containerName' "$command_file")
    local server_node0=$(jq -r '.serverNode0 // "172.16.0.4:8085"' "$command_file")
    local server_node1=$(jq -r '.serverNode1 // "172.16.0.4:8086"' "$command_file")
    local iterations=$(jq -r '.iterations // 1000' "$command_file")
    local concurrency=$(jq -r '.concurrency // 10' "$command_file")
    
    # Log to stderr so it doesn't get captured by command substitution
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Starting test for account: $account_name, container: $container_name" >&2
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Node 0: $server_node0, Node 1: $server_node1" >&2
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Iterations: $iterations, Concurrency: $concurrency" >&2
    
    local result_file="/tmp/kvtest-result-$job_id.log"
    
    # Write header to result file
    cat > "$result_file" <<EOF
===== KV Offload Test Started =====
Job ID: $job_id
Account: $account_name
Container: $container_name
Server Node 0: $server_node0
Server Node 1: $server_node1
Iterations: $iterations
Concurrency: $concurrency
Start Time: $(date '+%Y-%m-%d %H:%M:%S')
=====================================

EOF
    
    # Run tests on both nodes in parallel
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Launching Node 0 test..." >&2
    (
        export KVSTORE_GRPC_SERVER="$server_node0"
        export GRPC_VERBOSITY=error
        "$KVPLAYGROUND_PATH" "$CONVERSATION_JSON" "$iterations" "$concurrency" \
            -s "https://$account_name.prompts.azure.net" \
            -c "$container_name" \
            --log-level error 2>&1 | tee -a "$result_file.node0" >/dev/null
    ) &
    local pid0=$!
    
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Launching Node 1 test..." >&2
    (
        export KVSTORE_GRPC_SERVER="$server_node1"
        export GRPC_VERBOSITY=error
        "$KVPLAYGROUND_PATH" "$CONVERSATION_JSON" "$iterations" "$concurrency" \
            -s "https://$account_name.prompts.azure.net" \
            -c "$container_name" \
            --log-level error 2>&1 | tee -a "$result_file.node1" >/dev/null
    ) &
    local pid1=$!
    
    # Wait for both to complete
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Waiting for tests to complete (PIDs: $pid0, $pid1)..." >&2
    wait $pid0
    local exit0=$?
    wait $pid1
    local exit1=$?
    
    # Combine results
    echo "" >> "$result_file"
    echo "===== Node 0 Results =====" >> "$result_file"
    cat "$result_file.node0" >> "$result_file" 2>/dev/null
    echo "" >> "$result_file"
    echo "===== Node 1 Results =====" >> "$result_file"
    cat "$result_file.node1" >> "$result_file" 2>/dev/null
    echo "" >> "$result_file"
    echo "===== Test Completed =====" >> "$result_file"
    echo "End Time: $(date '+%Y-%m-%d %H:%M:%S')" >> "$result_file"
    echo "Node 0 Exit Code: $exit0" >> "$result_file"
    echo "Node 1 Exit Code: $exit1" >> "$result_file"
    
    if [ $exit0 -eq 0 ] && [ $exit1 -eq 0 ]; then
        echo "Status: SUCCESS" >> "$result_file"
        echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Test completed successfully" >&2
    else
        echo "Status: FAILED" >> "$result_file"
        echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - Test failed (Exit codes: $exit0, $exit1)" >&2
    fi
    
    # Cleanup
    rm -f "$result_file.node0" "$result_file.node1"
    
    echo "$result_file"
}

# Upload results to blob storage
upload_results() {
    local result_file=$1
    local job_id=$2
    local blob_name="result-$job_id.log"
    
    log_info "Uploading results: $blob_name"
    
    az storage blob upload \
        --container-name "$RESULTS_CONTAINER" \
        --name "$blob_name" \
        --file "$result_file" \
        --account-name "$STORAGE_ACCOUNT" \
        --auth-mode login \
        --overwrite \
        --output none 2>/dev/null
    
    if [ $? -eq 0 ]; then
        log_success "Results uploaded successfully"
        rm -f "$result_file"
        return 0
    else
        log_error "Failed to upload results"
        return 1
    fi
}

# Main watch loop
watch_for_commands() {
    log_info "Starting KV Offload Test Watcher..."
    log_info "Storage Account: $STORAGE_ACCOUNT"
    log_info "Command Container: $COMMAND_CONTAINER"
    log_info "Results Container: $RESULTS_CONTAINER"
    log_info "Poll Interval: ${POLL_INTERVAL}s"
    log_info "Press Ctrl+C to stop"
    echo ""
    
    while true; do
        # Check for pending command
        blob_name=$(get_pending_command)
        
        if [ $? -eq 0 ] && [ -n "$blob_name" ]; then
            log_success "Found pending command: $blob_name"
            
            # Extract job ID from blob name
            job_id=$(basename "$blob_name" .json)
            
            # Download command
            command_file=$(download_command "$blob_name")
            
            if [ $? -eq 0 ]; then
                # Execute test
                result_file=$(execute_test "$command_file" "$job_id")
                
                # Upload results
                upload_results "$result_file" "$job_id"
                
                # Delete command blob
                delete_command "$blob_name"
                
                # Cleanup
                rm -f "$command_file"
            fi
            
            echo ""
        else
            # No commands, just show alive signal every minute
            if [ $((SECONDS % 60)) -eq 0 ]; then
                echo -ne "\r${BLUE}[WATCHING]${NC} $(date '+%H:%M:%S') - Waiting for commands..."
            fi
        fi
        
        sleep "$POLL_INTERVAL"
    done
}

# Trap Ctrl+C
trap 'echo -e "\n${YELLOW}Shutting down watcher...${NC}"; exit 0' INT TERM

# Main
check_prerequisites
watch_for_commands
