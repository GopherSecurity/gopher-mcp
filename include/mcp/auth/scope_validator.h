#ifndef MCP_AUTH_SCOPE_VALIDATOR_H
#define MCP_AUTH_SCOPE_VALIDATOR_H

#include <string>
#include <vector>
#include <unordered_set>
#include <memory>

/**
 * @file scope_validator.h
 * @brief High-performance OAuth 2.1 scope validation with O(1) lookup
 */

namespace mcp {
namespace auth {

/**
 * @brief Scope comparison result
 */
enum class ScopeMatchResult {
  EXACT_MATCH,     // Scopes match exactly
  SUBSET,          // Required scopes are subset of available
  SUPERSET,        // Required scopes are superset of available  
  DISJOINT,        // No common scopes
  PARTIAL_MATCH    // Some scopes match but not all required
};

/**
 * @brief High-performance scope validator with O(1) lookup
 */
class ScopeValidator {
public:
  /**
   * @brief Parse scope string into individual scopes
   * @param scope_string Space-separated scope string
   * @return Vector of individual scope strings
   */
  static std::vector<std::string> parse_scope_string(const std::string& scope_string);
  
  /**
   * @brief Convert scope vector to space-separated string
   * @param scopes Vector of scope strings
   * @return Space-separated scope string
   */
  static std::string scopes_to_string(const std::vector<std::string>& scopes);
  
  /**
   * @brief Check if a scope matches a pattern (supports wildcards)
   * @param scope The scope to check
   * @param pattern The pattern to match against (can contain * for wildcard)
   * @return true if scope matches pattern
   */
  static bool matches_pattern(const std::string& scope, const std::string& pattern);
  
  /**
   * @brief Validate that required scopes are satisfied by available scopes
   * @param required_scopes Scopes that must be present
   * @param available_scopes Scopes that are available
   * @return true if all required scopes are satisfied
   */
  static bool validate_scopes(const std::vector<std::string>& required_scopes,
                              const std::vector<std::string>& available_scopes);
  
  /**
   * @brief Validate using hash sets for O(1) lookup performance
   * @param required_scopes Scopes that must be present  
   * @param available_scopes Scopes that are available
   * @return true if all required scopes are satisfied
   */
  static bool validate_scopes_fast(const std::unordered_set<std::string>& required_scopes,
                                   const std::unordered_set<std::string>& available_scopes);
  
  /**
   * @brief Compare two scope sets
   * @param scopes1 First scope set
   * @param scopes2 Second scope set
   * @return Comparison result
   */
  static ScopeMatchResult compare_scopes(const std::unordered_set<std::string>& scopes1,
                                         const std::unordered_set<std::string>& scopes2);
  
  /**
   * @brief Check scope hierarchy (e.g., "read:user" is satisfied by "read:*")
   * @param required_scope The specific scope required
   * @param available_scopes Available scopes (may contain wildcards)
   * @return true if required scope is satisfied by available scopes
   */
  static bool check_scope_hierarchy(const std::string& required_scope,
                                    const std::vector<std::string>& available_scopes);
  
  /**
   * @brief Builder for creating scope validators with predefined rules
   */
  class Builder {
  public:
    Builder();
    ~Builder();
    
    /**
     * @brief Add a wildcard pattern rule
     * @param pattern Wildcard pattern (e.g., "read:*")
     * @return Builder reference for chaining
     */
    Builder& add_wildcard_rule(const std::string& pattern);
    
    /**
     * @brief Add scope hierarchy rule
     * @param parent Parent scope that satisfies children
     * @param children Child scopes satisfied by parent
     * @return Builder reference for chaining
     */
    Builder& add_hierarchy_rule(const std::string& parent, 
                                const std::vector<std::string>& children);
    
    /**
     * @brief Enable strict mode (no wildcards or hierarchy)
     * @return Builder reference for chaining
     */
    Builder& strict_mode(bool enable = true);
    
    /**
     * @brief Build the scope validator
     * @return Configured scope validator
     */
    std::unique_ptr<ScopeValidator> build();
    
  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
  };
  
  /**
   * @brief Default constructor
   */
  ScopeValidator();
  
  /**
   * @brief Destructor
   */
  ~ScopeValidator();
  
  /**
   * @brief Validate scopes using configured rules
   * @param required_scopes Required scopes
   * @param available_scopes Available scopes  
   * @return true if validation passes
   */
  bool validate(const std::vector<std::string>& required_scopes,
               const std::vector<std::string>& available_scopes) const;
  
  /**
   * @brief Set strict mode
   * @param enable Enable/disable strict mode
   */
  void set_strict_mode(bool enable);
  
  /**
   * @brief Check if strict mode is enabled
   * @return true if strict mode is enabled
   */
  bool is_strict_mode() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * @brief Utility functions for OAuth 2.1 scope handling
 */
namespace scope_utils {
  
  /**
   * @brief Normalize a scope string (lowercase, trim whitespace)
   * @param scope Scope to normalize
   * @return Normalized scope string
   */
  std::string normalize_scope(const std::string& scope);
  
  /**
   * @brief Check if scope is valid according to OAuth 2.1 spec
   * @param scope Scope to validate
   * @return true if scope is valid
   */
  bool is_valid_scope(const std::string& scope);
  
  /**
   * @brief Extract scope namespace (part before ':')
   * @param scope Scope string (e.g., "read:user")
   * @return Namespace part (e.g., "read") or empty if no namespace
   */
  std::string get_scope_namespace(const std::string& scope);
  
  /**
   * @brief Extract scope resource (part after ':')
   * @param scope Scope string (e.g., "read:user")
   * @return Resource part (e.g., "user") or full scope if no separator
   */
  std::string get_scope_resource(const std::string& scope);
  
  /**
   * @brief Compute intersection of two scope sets
   * @param set1 First scope set
   * @param set2 Second scope set
   * @return Common scopes
   */
  std::unordered_set<std::string> scope_intersection(
      const std::unordered_set<std::string>& set1,
      const std::unordered_set<std::string>& set2);
  
  /**
   * @brief Compute union of two scope sets
   * @param set1 First scope set
   * @param set2 Second scope set
   * @return Combined scopes
   */
  std::unordered_set<std::string> scope_union(
      const std::unordered_set<std::string>& set1,
      const std::unordered_set<std::string>& set2);
  
  /**
   * @brief Compute difference of two scope sets (set1 - set2)
   * @param set1 First scope set
   * @param set2 Second scope set
   * @return Scopes in set1 but not in set2
   */
  std::unordered_set<std::string> scope_difference(
      const std::unordered_set<std::string>& set1,
      const std::unordered_set<std::string>& set2);
  
} // namespace scope_utils

} // namespace auth
} // namespace mcp

#endif // MCP_AUTH_SCOPE_VALIDATOR_H