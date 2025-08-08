#\!/bin/bash

# Update includes for moved headers
echo "Updating include paths..."

# Core headers
find . -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" \) -not -path "./build/*" -not -path "./.git/*" | while read file; do
    # Update core includes
    sed -i '' 's|#include "mcp/optional.h"|#include "mcp/core/optional.h"|g' "$file"
    sed -i '' 's|#include "mcp/variant.h"|#include "mcp/core/variant.h"|g' "$file"
    sed -i '' 's|#include "mcp/result.h"|#include "mcp/core/result.h"|g' "$file"
    sed -i '' 's|#include "mcp/compat.h"|#include "mcp/core/compat.h"|g' "$file"
    sed -i '' 's|#include "mcp/memory_utils.h"|#include "mcp/core/memory_utils.h"|g' "$file"
    sed -i '' 's|#include "mcp/type_helpers.h"|#include "mcp/core/type_helpers.h"|g' "$file"
    
    # JSON headers
    sed -i '' 's|#include "mcp/json_bridge.h"|#include "mcp/json/json_bridge.h"|g' "$file"
    sed -i '' 's|#include "mcp/json_serialization.h"|#include "mcp/json/json_serialization.h"|g' "$file"
    sed -i '' 's|#include "mcp/json_serialization_mcp_traits.h"|#include "mcp/json/json_serialization_mcp_traits.h"|g' "$file"
done

echo "Done updating includes"
