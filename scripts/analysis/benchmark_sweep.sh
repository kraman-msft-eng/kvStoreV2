#!/bin/bash
# Comprehensive Benchmark Sweep Script
# Runs 8 processes with varying concurrency, captures Lookup and Read latencies
# Outputs CSV for charting

set -e

GRPC_SERVER_NUMA0="10.0.0.4:8085"
GRPC_SERVER_NUMA1="10.0.0.4:8086"
STORAGE_URL="https://aoaikv.prompts.azure.net"
CONTAINER="gpt41-promptcache"
ITERATIONS_PER_PROCESS=100
OUTPUT_DIR="/tmp/benchmark_sweep"
CSV_FILE="$OUTPUT_DIR/benchmark_results.csv"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# CSV header
echo "processes,concurrency_per_process,total_concurrency,tps,tokens_per_sec,lookup_p50_ms,lookup_p90_ms,lookup_p99_ms,lookup_p100_ms,read_p50_ms,read_p90_ms,read_p99_ms,read_p100_ms" > "$CSV_FILE"

cd ~/kvgrpc
export GRPC_VERBOSITY=error

echo "=============================================="
echo "  KVStore Benchmark Sweep"
echo "  8 Processes × Variable Concurrency"
echo "=============================================="
echo ""

# Function to extract percentile from log
extract_percentile() {
    local file=$1
    local operation=$2  # "Lookup" or "Read"
    local percentile=$3 # "p50", "p90", "p95", "p99"
    
    # Extract the Client E2E line after the operation header
    grep -A2 "^${operation} (" "$file" 2>/dev/null | grep "Client E2E:" | \
        sed -n "s/.*${percentile}=\([0-9.]*\)ms.*/\1/p" | head -1
}

# Function to run benchmark with N processes and C concurrency
run_benchmark() {
    local num_processes=$1
    local concurrency=$2
    local total_concurrency=$((num_processes * concurrency))
    
    echo ""
    echo ">>> Running: $num_processes processes × $concurrency concurrency = $total_concurrency total"
    
    # Clean up old logs
    rm -f /tmp/proc_*.log
    
    # Start time
    START=$(date +%s.%N)
    
    # Launch processes - distribute across NUMA nodes
    for i in $(seq 1 $num_processes); do
        if [ $((i % 2)) -eq 1 ]; then
            export KVSTORE_GRPC_SERVER="$GRPC_SERVER_NUMA0"
        else
            export KVSTORE_GRPC_SERVER="$GRPC_SERVER_NUMA1"
        fi
        ./KVPlayground conversation_tokens.json $ITERATIONS_PER_PROCESS $concurrency \
            -s "$STORAGE_URL" -c "$CONTAINER" --log-level error \
            > /tmp/proc_${i}.log 2>&1 &
    done
    
    # Wait for all processes
    wait
    
    # End time
    END=$(date +%s.%N)
    ELAPSED=$(echo "$END - $START" | bc)
    
    # Calculate total iterations and TPS
    TOTAL_ITERATIONS=$((num_processes * ITERATIONS_PER_PROCESS))
    TPS=$(echo "scale=2; $TOTAL_ITERATIONS / $ELAPSED" | bc)
    
    # Tokens: 6 reads × 128 + 3 writes × 128 = 1152 tokens per iteration
    TOKENS_PER_ITER=1152
    TOKENS_PER_SEC=$(echo "scale=0; $TPS * $TOKENS_PER_ITER" | bc)
    KTOKENS_PER_SEC=$(echo "scale=2; $TOKENS_PER_SEC / 1000" | bc)
    
    # Aggregate percentiles from all process logs
    # Collect all values and compute aggregate
    ALL_LOOKUP_P50=""
    ALL_LOOKUP_P90=""
    ALL_LOOKUP_P99=""
    ALL_LOOKUP_P100=""
    ALL_READ_P50=""
    ALL_READ_P90=""
    ALL_READ_P99=""
    ALL_READ_P100=""
    
    for i in $(seq 1 $num_processes); do
        L50=$(extract_percentile /tmp/proc_${i}.log "Lookup" "p50")
        L90=$(extract_percentile /tmp/proc_${i}.log "Lookup" "p90")
        L99=$(extract_percentile /tmp/proc_${i}.log "Lookup" "p99")
        L100=$(extract_percentile /tmp/proc_${i}.log "Lookup" "p100")
        R50=$(extract_percentile /tmp/proc_${i}.log "Read" "p50")
        R90=$(extract_percentile /tmp/proc_${i}.log "Read" "p90")
        R99=$(extract_percentile /tmp/proc_${i}.log "Read" "p99")
        R100=$(extract_percentile /tmp/proc_${i}.log "Read" "p100")
        
        [ -n "$L50" ] && ALL_LOOKUP_P50="$ALL_LOOKUP_P50 $L50"
        [ -n "$L90" ] && ALL_LOOKUP_P90="$ALL_LOOKUP_P90 $L90"
        [ -n "$L99" ] && ALL_LOOKUP_P99="$ALL_LOOKUP_P99 $L99"
        [ -n "$L100" ] && ALL_LOOKUP_P100="$ALL_LOOKUP_P100 $L100"
        [ -n "$R50" ] && ALL_READ_P50="$ALL_READ_P50 $R50"
        [ -n "$R90" ] && ALL_READ_P90="$ALL_READ_P90 $R90"
        [ -n "$R99" ] && ALL_READ_P99="$ALL_READ_P99 $R99"
        [ -n "$R100" ] && ALL_READ_P100="$ALL_READ_P100 $R100"
    done
    
    # Average the percentiles across processes
    avg() {
        echo "$@" | tr ' ' '\n' | grep -v '^$' | awk '{sum+=$1; count++} END {if(count>0) printf "%.2f", sum/count; else print "0"}'
    }
    
    LOOKUP_P50=$(avg $ALL_LOOKUP_P50)
    LOOKUP_P90=$(avg $ALL_LOOKUP_P90)
    LOOKUP_P99=$(avg $ALL_LOOKUP_P99)
    LOOKUP_P100=$(avg $ALL_LOOKUP_P100)
    READ_P50=$(avg $ALL_READ_P50)
    READ_P90=$(avg $ALL_READ_P90)
    READ_P99=$(avg $ALL_READ_P99)
    READ_P100=$(avg $ALL_READ_P100)
    
    # Print summary
    echo "    Elapsed: ${ELAPSED}s | TPS: $TPS | Tokens/s: ${KTOKENS_PER_SEC}K"
    echo "    Lookup: p50=${LOOKUP_P50}ms p90=${LOOKUP_P90}ms p99=${LOOKUP_P99}ms p100=${LOOKUP_P100}ms"
    echo "    Read:   p50=${READ_P50}ms p90=${READ_P90}ms p99=${READ_P99}ms p100=${READ_P100}ms"
    
    # Append to CSV
    echo "$num_processes,$concurrency,$total_concurrency,$TPS,$TOKENS_PER_SEC,$LOOKUP_P50,$LOOKUP_P90,$LOOKUP_P99,$LOOKUP_P100,$READ_P50,$READ_P90,$READ_P99,$READ_P100" >> "$CSV_FILE"
}

echo ""
echo "=== Phase 1: Scaling Processes (1 concurrency each) ==="
for P in 1 2 4 6 8; do
    run_benchmark $P 1
done

echo ""
echo "=== Phase 2: Scaling Concurrency (8 processes) ==="
for C in 2 4 8 10; do
    run_benchmark 8 $C
done

echo ""
echo "=============================================="
echo "  Benchmark Complete!"
echo "  Results saved to: $CSV_FILE"
echo "=============================================="
echo ""
cat "$CSV_FILE"
