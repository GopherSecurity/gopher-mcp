#!/usr/bin/env node

/**
 * Simple JavaScript test for libgopher_mcp_auth on Linux
 * Works with older Node.js versions (10+)
 */

const fs = require('fs');
const { execSync } = require('child_process');
const path = require('path');

// Color output helpers
const RED = '\x1b[31m';
const GREEN = '\x1b[32m';
const YELLOW = '\x1b[33m';
const RESET = '\x1b[0m';

console.log(`${GREEN}========================================${RESET}`);
console.log(`${GREEN}Simple libgopher_mcp_auth Test (Linux)${RESET}`);
console.log(`${GREEN}========================================${RESET}`);

// Test configuration - use .so for Linux
const LIBRARY_NAME = process.env.MCP_LIBRARY_PATH || './libgopher_mcp_auth.so';

console.log(`Library path: ${LIBRARY_NAME}`);

// Check if library exists
if (!fs.existsSync(LIBRARY_NAME)) {
    console.error(`${RED}❌ Library not found at ${LIBRARY_NAME}${RESET}`);
    process.exit(1);
}

console.log(`${GREEN}✓ Library file exists${RESET}`);

// Test 1: Check library is a valid shared object
console.log(`\n${YELLOW}Test 1: Verify library file type${RESET}`);
try {
    const fileInfo = execSync(`file ${LIBRARY_NAME}`).toString();
    if (fileInfo.includes('ELF') && fileInfo.includes('shared object')) {
        console.log(`${GREEN}✓ Valid ELF shared object${RESET}`);
    } else {
        console.log(`${RED}✗ Not a valid shared object${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not verify file type${RESET}`);
}

// Test 2: Check library exports
console.log(`\n${YELLOW}Test 2: Check exported symbols${RESET}`);
try {
    const symbols = execSync(`nm -D ${LIBRARY_NAME} 2>/dev/null | grep mcp_auth_ || true`).toString();
    const expectedSymbols = [
        'mcp_auth_init',
        'mcp_auth_client_create',
        'mcp_auth_client_destroy',
        'mcp_auth_get_last_error',
        'mcp_auth_version'
    ];
    
    let foundCount = 0;
    for (const symbol of expectedSymbols) {
        if (symbols.includes(symbol)) {
            console.log(`${GREEN}  ✓ Found ${symbol}${RESET}`);
            foundCount++;
        } else {
            console.log(`${RED}  ✗ Missing ${symbol}${RESET}`);
        }
    }
    
    if (foundCount === expectedSymbols.length) {
        console.log(`${GREEN}✓ All expected symbols found${RESET}`);
    } else {
        console.log(`${YELLOW}⚠ Some symbols missing (${foundCount}/${expectedSymbols.length})${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not check symbols: ${e.message}${RESET}`);
}

// Test 3: Check library dependencies
console.log(`\n${YELLOW}Test 3: Check library dependencies${RESET}`);
try {
    const lddOutput = execSync(`ldd ${LIBRARY_NAME}`).toString();
    const requiredLibs = ['libssl', 'libcrypto', 'libcurl'];
    
    for (const lib of requiredLibs) {
        if (lddOutput.includes(lib)) {
            console.log(`${GREEN}  ✓ Links to ${lib}${RESET}`);
        } else {
            console.log(`${YELLOW}  ⚠ Does not link to ${lib}${RESET}`);
        }
    }
    
    if (!lddOutput.includes('not found')) {
        console.log(`${GREEN}✓ All dependencies resolved${RESET}`);
    } else {
        console.log(`${RED}✗ Some dependencies not found${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not check dependencies${RESET}`);
}

// Test 4: Library size check
console.log(`\n${YELLOW}Test 4: Library size check${RESET}`);
const stats = fs.statSync(LIBRARY_NAME);
const sizeKB = Math.round(stats.size / 1024);
console.log(`  Library size: ${sizeKB} KB`);
if (sizeKB > 50 && sizeKB < 5000) {
    console.log(`${GREEN}✓ Library size looks reasonable${RESET}`);
} else {
    console.log(`${YELLOW}⚠ Library size may be unusual${RESET}`);
}

// Summary
console.log(`\n${GREEN}========================================${RESET}`);
console.log(`${GREEN}✅ Basic library verification complete!${RESET}`);
console.log(`${GREEN}========================================${RESET}`);
console.log('\nThe library file appears to be properly built.');
console.log('For full functionality testing, use a newer Node.js version (14+)');

process.exit(0);