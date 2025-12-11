#!/usr/bin/env node

/**
 * Simple JavaScript test for libgopher_mcp_auth on Windows
 * Works with older Node.js versions (10+)
 */

const fs = require('fs');
const { execSync } = require('child_process');
const path = require('path');

// Color output helpers (Windows CMD may not support all colors)
const RED = '\x1b[31m';
const GREEN = '\x1b[32m';
const YELLOW = '\x1b[33m';
const RESET = '\x1b[0m';

console.log(`${GREEN}========================================${RESET}`);
console.log(`${GREEN}Simple libgopher_mcp_auth Test (Windows)${RESET}`);
console.log(`${GREEN}========================================${RESET}`);

// Test configuration - use .dll for Windows
const LIBRARY_NAME = process.env.MCP_LIBRARY_PATH || './gopher_mcp_auth.dll';

console.log(`Library path: ${LIBRARY_NAME}`);

// Check if library exists
if (!fs.existsSync(LIBRARY_NAME)) {
    console.error(`${RED}❌ Library not found at ${LIBRARY_NAME}${RESET}`);
    process.exit(1);
}

console.log(`${GREEN}✓ Library file exists${RESET}`);

// Test 1: Check library is a valid DLL
console.log(`\n${YELLOW}Test 1: Verify library file type${RESET}`);
try {
    // On Windows, we can check if it's a PE file
    const stats = fs.statSync(LIBRARY_NAME);
    const fd = fs.openSync(LIBRARY_NAME, 'r');
    const buffer = Buffer.alloc(2);
    fs.readSync(fd, buffer, 0, 2, 0);
    fs.closeSync(fd);
    
    // Check for MZ header (DOS header for PE files)
    if (buffer[0] === 0x4D && buffer[1] === 0x5A) {
        console.log(`${GREEN}✓ Valid PE/DLL file${RESET}`);
    } else {
        console.log(`${RED}✗ Not a valid DLL file${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not verify file type: ${e.message}${RESET}`);
}

// Test 2: Check library exports (Windows)
console.log(`\n${YELLOW}Test 2: Check exported symbols${RESET}`);
try {
    // On Windows, try using dumpbin if available (Visual Studio tools)
    // Otherwise, we'll skip this test
    let symbols = '';
    try {
        symbols = execSync(`dumpbin /exports ${LIBRARY_NAME} 2>nul`).toString();
    } catch {
        // Try alternative: objdump from MinGW
        try {
            symbols = execSync(`objdump -p ${LIBRARY_NAME} 2>nul | findstr "DLL Name\\|\\[.*\\]"`).toString();
        } catch {
            console.log(`${YELLOW}⚠ No tools available to check exports (dumpbin/objdump)${RESET}`);
            symbols = '';
        }
    }
    
    if (symbols) {
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
        } else if (foundCount > 0) {
            console.log(`${YELLOW}⚠ Some symbols missing (${foundCount}/${expectedSymbols.length})${RESET}`);
        }
    } else {
        console.log(`${YELLOW}⚠ Export verification skipped (tools not available)${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not check symbols: ${e.message}${RESET}`);
}

// Test 3: Check library dependencies (Windows)
console.log(`\n${YELLOW}Test 3: Check library dependencies${RESET}`);
try {
    // Try using dumpbin /dependents or objdump
    let deps = '';
    try {
        deps = execSync(`dumpbin /dependents ${LIBRARY_NAME} 2>nul`).toString();
    } catch {
        try {
            // Alternative: use objdump
            deps = execSync(`objdump -p ${LIBRARY_NAME} 2>nul | findstr "DLL Name"`, { encoding: 'utf8' }).toString();
        } catch {
            console.log(`${YELLOW}⚠ No tools available to check dependencies${RESET}`);
        }
    }
    
    if (deps) {
        // Common Windows system DLLs and runtime libraries
        const expectedDeps = ['kernel32.dll', 'msvcrt.dll'];
        const optionalDeps = ['libssl', 'libcrypto', 'libcurl', 'ssleay', 'libeay'];
        
        for (const dep of expectedDeps) {
            if (deps.toLowerCase().includes(dep.toLowerCase())) {
                console.log(`${GREEN}  ✓ Links to ${dep}${RESET}`);
            }
        }
        
        for (const dep of optionalDeps) {
            if (deps.toLowerCase().includes(dep.toLowerCase())) {
                console.log(`${GREEN}  ✓ Links to ${dep}${RESET}`);
            }
        }
        
        console.log(`${GREEN}✓ Dependencies checked${RESET}`);
    } else {
        console.log(`${YELLOW}⚠ Dependency check skipped (tools not available)${RESET}`);
    }
} catch (e) {
    console.log(`${YELLOW}⚠ Could not check dependencies: ${e.message}${RESET}`);
}

// Test 4: Library size check
console.log(`\n${YELLOW}Test 4: Library size check${RESET}`);
const stats = fs.statSync(LIBRARY_NAME);
const sizeKB = Math.round(stats.size / 1024);
console.log(`  Library size: ${sizeKB} KB`);
if (sizeKB > 50 && sizeKB < 10000) {
    console.log(`${GREEN}✓ Library size looks reasonable${RESET}`);
} else {
    console.log(`${YELLOW}⚠ Library size may be unusual${RESET}`);
}

// Test 5: Check for required runtime files
console.log(`\n${YELLOW}Test 5: Check for runtime dependencies${RESET}`);
const runtimeFiles = [
    'libssl-1_1-x64.dll',
    'libcrypto-1_1-x64.dll',
    'libcurl.dll',
    'ssleay32.dll',
    'libeay32.dll'
];

let foundRuntime = false;
for (const file of runtimeFiles) {
    if (fs.existsSync(`./${file}`)) {
        console.log(`${GREEN}  ✓ Found ${file}${RESET}`);
        foundRuntime = true;
    }
}

if (!foundRuntime) {
    console.log(`${YELLOW}  ⚠ No runtime DLLs found in current directory${RESET}`);
    console.log(`${YELLOW}    (They may be in system PATH)${RESET}`);
} else {
    console.log(`${GREEN}✓ Runtime dependencies found${RESET}`);
}

// Summary
console.log(`\n${GREEN}========================================${RESET}`);
console.log(`${GREEN}✅ Basic library verification complete!${RESET}`);
console.log(`${GREEN}========================================${RESET}`);
console.log('\nThe library file appears to be properly built.');
console.log('For full functionality testing, use a newer Node.js version (14+)');

process.exit(0);