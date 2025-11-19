#!/bin/bash
# test-health.sh - Health check and monitoring for MCP server

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
SERVER_URL="${SERVER_URL:-http://127.0.0.1:8080}"
CHECK_INTERVAL="${CHECK_INTERVAL:-5}"  # seconds
MAX_CHECKS="${MAX_CHECKS:-0}"  # 0 = infinite
ALERT_THRESHOLD="${ALERT_THRESHOLD:-3}"  # consecutive failures before alert

# Counters
CHECKS_PERFORMED=0
CONSECUTIVE_FAILURES=0
TOTAL_FAILURES=0
TOTAL_SUCCESS=0
START_TIME=$(date +%s)

echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}🏥 MCP Server Health Monitor${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}Server: $SERVER_URL${NC}"
echo -e "${BLUE}Check Interval: ${CHECK_INTERVAL}s${NC}"
echo -e "${BLUE}Alert Threshold: ${ALERT_THRESHOLD} failures${NC}"
echo -e "${BLUE}Max Checks: $([ $MAX_CHECKS -eq 0 ] && echo "∞" || echo $MAX_CHECKS)${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n"

# Cleanup on exit
cleanup() {
    echo -e "\n${YELLOW}Stopping health monitor...${NC}"
    show_statistics
    exit 0
}

trap cleanup EXIT INT TERM

# Function to format duration
format_duration() {
    local duration=$1
    local hours=$((duration / 3600))
    local minutes=$(((duration % 3600) / 60))
    local seconds=$((duration % 60))
    
    if [ $hours -gt 0 ]; then
        printf "%dh %dm %ds" $hours $minutes $seconds
    elif [ $minutes -gt 0 ]; then
        printf "%dm %ds" $minutes $seconds
    else
        printf "%ds" $seconds
    fi
}

# Function to show statistics
show_statistics() {
    local END_TIME=$(date +%s)
    local DURATION=$((END_TIME - START_TIME))
    local UPTIME_PERCENTAGE=0
    
    if [ $CHECKS_PERFORMED -gt 0 ]; then
        UPTIME_PERCENTAGE=$((TOTAL_SUCCESS * 100 / CHECKS_PERFORMED))
    fi
    
    echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}📊 Health Check Statistics${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "Monitoring Duration: $(format_duration $DURATION)"
    echo -e "Total Checks: ${CHECKS_PERFORMED}"
    echo -e "${GREEN}Successful: ${TOTAL_SUCCESS}${NC}"
    echo -e "${RED}Failed: ${TOTAL_FAILURES}${NC}"
    echo -e "Uptime: ${UPTIME_PERCENTAGE}%"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# Function to check server health
check_health() {
    local TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    local RESPONSE=""
    local HTTP_CODE=""
    local RESPONSE_TIME=""
    
    # Measure response time
    local START=$(date +%s%N)
    
    # Make health check request
    RESPONSE=$(curl -s -w "\n%{http_code}" "${SERVER_URL}/health" 2>/dev/null || echo "000")
    HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
    RESPONSE_BODY=$(echo "$RESPONSE" | head -n-1)
    
    local END=$(date +%s%N)
    RESPONSE_TIME=$(( (END - START) / 1000000 ))  # Convert to milliseconds
    
    CHECKS_PERFORMED=$((CHECKS_PERFORMED + 1))
    
    # Check if server is healthy
    if [ "$HTTP_CODE" = "200" ] && echo "$RESPONSE_BODY" | grep -q "ok"; then
        echo -e "[$TIMESTAMP] ${GREEN}✅ HEALTHY${NC} - Response: ${RESPONSE_TIME}ms"
        CONSECUTIVE_FAILURES=0
        TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
        return 0
    else
        echo -e "[$TIMESTAMP] ${RED}❌ UNHEALTHY${NC} - HTTP $HTTP_CODE"
        CONSECUTIVE_FAILURES=$((CONSECUTIVE_FAILURES + 1))
        TOTAL_FAILURES=$((TOTAL_FAILURES + 1))
        
        # Alert if threshold reached
        if [ $CONSECUTIVE_FAILURES -ge $ALERT_THRESHOLD ]; then
            echo -e "${RED}⚠️  ALERT: Server has been down for $CONSECUTIVE_FAILURES consecutive checks!${NC}"
        fi
        
        return 1
    fi
}

# Function to check detailed server status
check_detailed_status() {
    echo -e "\n${BLUE}🔍 Performing detailed health check...${NC}"
    
    # Check health endpoint
    echo -n "  Health endpoint: "
    if curl -s -f "${SERVER_URL}/health" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"
    fi
    
    # Check MCP endpoint
    echo -n "  MCP endpoint: "
    TOOLS_RESPONSE=$(curl -s -X POST "${SERVER_URL}/mcp" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' 2>/dev/null)
    
    if echo "$TOOLS_RESPONSE" | grep -q "result"; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"
    fi
    
    # Check available tools
    echo -n "  Available tools: "
    TOOL_COUNT=$(echo "$TOOLS_RESPONSE" | grep -o '"name"' | wc -l)
    if [ $TOOL_COUNT -gt 0 ]; then
        echo -e "${GREEN}$TOOL_COUNT tools${NC}"
    else
        echo -e "${RED}No tools found${NC}"
    fi
    
    # Test a simple calculation
    echo -n "  Calculation test: "
    CALC_RESPONSE=$(curl -s -X POST "${SERVER_URL}/mcp" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"calculate","arguments":{"operation":"add","a":1,"b":1}}}' 2>/dev/null)
    
    if echo "$CALC_RESPONSE" | grep -q "2"; then
        echo -e "${GREEN}✓ (1+1=2)${NC}"
    else
        echo -e "${RED}✗${NC}"
    fi
    
    echo ""
}

# Function to monitor mode
monitor_mode() {
    echo -e "${YELLOW}Starting continuous monitoring (Ctrl+C to stop)...${NC}\n"
    
    while true; do
        check_health
        
        # Check if we've reached max checks
        if [ $MAX_CHECKS -gt 0 ] && [ $CHECKS_PERFORMED -ge $MAX_CHECKS ]; then
            echo -e "\n${YELLOW}Reached maximum number of checks ($MAX_CHECKS)${NC}"
            break
        fi
        
        # Perform detailed check every 10 checks
        if [ $((CHECKS_PERFORMED % 10)) -eq 0 ] && [ $CHECKS_PERFORMED -gt 0 ]; then
            check_detailed_status
        fi
        
        sleep $CHECK_INTERVAL
    done
}

# Function for single check mode
single_check_mode() {
    echo -e "${YELLOW}Performing single health check...${NC}\n"
    
    if check_health; then
        check_detailed_status
        echo -e "\n${GREEN}✅ Server is healthy${NC}"
        exit 0
    else
        echo -e "\n${RED}❌ Server is not healthy${NC}"
        exit 1
    fi
}

# Parse command line arguments
SINGLE_CHECK=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --once|-o)
            SINGLE_CHECK=true
            shift
            ;;
        --interval|-i)
            CHECK_INTERVAL="$2"
            shift 2
            ;;
        --max|-m)
            MAX_CHECKS="$2"
            shift 2
            ;;
        --url|-u)
            SERVER_URL="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -o, --once          Perform single health check and exit"
            echo "  -i, --interval SEC  Check interval in seconds (default: 5)"
            echo "  -m, --max COUNT     Maximum number of checks (0=infinite)"
            echo "  -u, --url URL       Server URL (default: http://127.0.0.1:8080)"
            echo "  -h, --help          Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                  # Continuous monitoring with defaults"
            echo "  $0 --once           # Single health check"
            echo "  $0 --interval 10    # Check every 10 seconds"
            echo "  $0 --max 100        # Stop after 100 checks"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Main execution
if [ "$SINGLE_CHECK" = true ]; then
    single_check_mode
else
    monitor_mode
fi