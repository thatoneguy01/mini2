#!/bin/bash

# Run 9-node basecamp cluster and launch client

CONFIG_PATH="${1:-config/grid_nodes.csv}"
LOG_DIR="${2:-logs}"
LOG_INTERVAL="${3:-1000}"
STARTUP_DELAY="${4:-2}"

# Create logs directory
mkdir -p "$LOG_DIR"
echo "Created $LOG_DIR directory"

# Read grid configuration
declare -A NODE_LANGUAGE
declare -A NODE_PORT
declare -a NODE_ORDER

while IFS=, read -r node_id host port language row col is_entry; do
    if [[ "$node_id" == "node_id" ]]; then
        continue  # Skip header
    fi
    if [[ -z "$node_id" ]]; then
        continue  # Skip empty lines
    fi
    
    # Trim whitespace
    node_id=$(echo "$node_id" | xargs)
    host=$(echo "$host" | xargs)
    port=$(echo "$port" | xargs)
    language=$(echo "$language" | xargs)
    
    NODE_LANGUAGE["$node_id"]="$language"
    NODE_PORT["$node_id"]="$port"
    NODE_ORDER+=("$node_id")
done < "$CONFIG_PATH"

echo "Loaded ${#NODE_ORDER[@]} nodes from $CONFIG_PATH"
echo "Nodes: ${NODE_ORDER[*]}"
echo ""

# Start all nodes
declare -A PIDS

for node_id in "${NODE_ORDER[@]}"; do
    language="${NODE_LANGUAGE[$node_id]}"
    port="${NODE_PORT[$node_id]}"
    log_file="$LOG_DIR/node_$node_id.log"
    
    echo "Starting Node $node_id ($language, :$port)..."
    
    if [[ "$language" == "cpp" || "$language" == "C++" ]]; then
        # Start C++ server
        if [[ ! -f "build/basecamp_cpp_server" ]]; then
            echo "  ERROR: build/basecamp_cpp_server not found. Run CMake build first."
            continue
        fi
        ./build/basecamp_cpp_server \
            --node-id "$node_id" \
            --config "$CONFIG_PATH" \
            --log-file "$log_file" \
            --log-interval "$LOG_INTERVAL" \
            > /dev/null 2>&1 &
    else
        # Start Python server
        python3 python/node_server.py \
            --node-id "$node_id" \
            --config "$CONFIG_PATH" \
            --log-file "$log_file" \
            --log-interval "$LOG_INTERVAL" \
            > /dev/null 2>&1 &
    fi
    
    pid=$!
    PIDS["$node_id"]=$pid
    echo "  Started (PID: $pid)"
done

echo ""
echo "Waiting $STARTUP_DELAY seconds for nodes to initialize..."
sleep "$STARTUP_DELAY"

# Launch client
echo ""
echo "Launching client..."

if [[ -f "build/basecamp_cpp_client" ]]; then
    ./build/basecamp_cpp_client --config "$CONFIG_PATH"
else
    echo "ERROR: build/basecamp_cpp_client not found"
fi

echo ""
echo "Client completed. Cluster is still running."
echo "Press Ctrl+C to stop all nodes."
echo "Log files: $LOG_DIR/*.log"

# Keep script alive and monitor nodes
cleanup() {
    echo ""
    echo "Terminating all nodes..."
    for node_id in "${NODE_ORDER[@]}"; do
        pid=${PIDS[$node_id]}
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            echo "Stopped Node $node_id"
        fi
    done
    echo "All nodes stopped."
    exit 0
}

trap cleanup SIGINT SIGTERM

while true; do
    sleep 5
    for node_id in "${NODE_ORDER[@]}"; do
        pid=${PIDS[$node_id]}
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "Node $node_id (PID $pid) has exited"
        fi
    done
done
