/**
 * Tool arguments that a server asks to be mirrored into HTTP headers.
 *
 * A tool's input schema may mark a parameter with `x-mcp-header`, and a
 * client sending that tool's call then copies the argument's value into
 * `Mcp-Param-{name}` beside the body. The point is the same as for the
 * standard mirrored headers: something between the two ends can route or
 * rate-limit on a value without parsing a JSON body.
 *
 * Which is only safe while the header and the body agree, so both ends
 * need the same answer to "which parameters does this tool designate, and
 * where does each one live". That answer is here, derived from the schema
 * once rather than re-derived at each call site.
 *
 * The constraints are strict and a violation invalidates the whole tool
 * definition rather than just the annotation. That is deliberate: a
 * parameter that cannot be reached by a fixed path, or that has no single
 * header form, would make the two ends disagree about a request neither
 * of them got wrong.
 */

#pragma once

#include <string>
#include <vector>

#include "mcp/core/result.h"
#include "mcp/json/json_bridge.h"
#include "mcp/protocol/modern_era.h"
#include "mcp/types.h"

namespace mcp {
namespace protocol {
namespace modern {

/** One argument a tool asks to have carried in a header as well. */
struct DesignatedParam {
  /** What follows `Mcp-Param-` in the header name. */
  std::string header_name;

  /**
   * Where the value lives in a call's arguments: the chain of property
   * names from the arguments object down to it. Always at least one long,
   * and every step is a `properties` key — which is what makes the value
   * findable without evaluating the schema.
   */
  std::vector<std::string> path;

  /** The full header name, as it goes on the wire. */
  std::string headerName() const {
    return std::string(kParamHeaderPrefix) + header_name;
  }
};

/**
 * The parameters a tool designates.
 *
 * @return An error naming the tool and the first violation found, which
 *         is what a server logs when it refuses to register the tool and
 *         what a client logs when it drops it from a listing. Success
 *         with an empty list is the ordinary case: most tools designate
 *         nothing.
 */
VoidResult designatedParams(const Tool& tool,
                            std::vector<DesignatedParam>* out);

/** Whether a tool's annotations are usable at all. */
inline VoidResult validateHeaderAnnotations(const Tool& tool) {
  std::vector<DesignatedParam> ignored;
  return designatedParams(tool, &ignored);
}

/**
 * The value at a designated path, if the call carries one.
 *
 * Absent is not a fault: a client omits the header for an argument it did
 * not send, and a server must not expect one. Only a value that is
 * present in the body and missing from the headers is a disagreement.
 */
bool valueAtPath(const json::JsonValue& arguments,
                 const std::vector<std::string>& path,
                 json::JsonValue* value);

/**
 * Whether an integer can be carried exactly.
 *
 * Values outside the range JavaScript can hold exactly are rejected
 * rather than rounded, since a header and a body that round differently
 * would disagree about a number neither end changed.
 */
bool isExactlyCarryableInteger(int64_t value);

/**
 * What a server needs in order to check the mirrored headers of a call.
 *
 * An interface rather than the tool registry itself: the transport has no
 * business knowing what a tool registry is, and a deployment that
 * designates no parameters carries none of this.
 */
class DesignatedParamLookup {
 public:
  virtual ~DesignatedParamLookup() = default;

  /**
   * The parameters this tool designates.
   * @return False when the tool is unknown, which is not this layer's
   *         business to refuse — the layer that owns the tools answers
   *         that, and refusing here would refuse it first and worse.
   */
  virtual bool paramsForTool(const std::string& tool_name,
                             std::vector<DesignatedParam>* out) const = 0;
};

}  // namespace modern
}  // namespace protocol
}  // namespace mcp
