// Copyright 2025 Gopher Security, Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * Elicitation, typed, in either revision.
 *
 * A server asks the user for input through the client. What it asks and
 * what comes back are the same in every revision; only how they travel
 * differs:
 *
 *   older revisions   the server sends an elicitation/create request of its
 *                     own, and the client answers it
 *   2026-07-28        the server answers the client's request with
 *                     input_required, naming the elicitation under
 *                     inputRequests, and the client sends the whole request
 *                     again with the answer under inputResponses
 *
 * These turn the typed ElicitRequest and ElicitResult into what each of
 * those paths carries, and back.
 */

#pragma once

#include "mcp/json/json_bridge.h"
#include "mcp/protocol/mrtr.h"
#include "mcp/types.h"

namespace mcp {
namespace protocol {
namespace elicitation {

/**
 * The elicitation as a request of its own, for a client of an older
 * revision: hand it to McpServer::sendRequest or McpServer::askClient.
 *
 * @param id An id no other outstanding request to that client uses.
 */
jsonrpc::Request toRequest(const ElicitRequest& elicit, const RequestId& id);

/**
 * The elicitation as an entry in inputRequests, for a 2026-07-28 client:
 * put it in a NeedsInput for McpServer::answerWithInput.
 */
modern::InputRequest toInputRequest(const ElicitRequest& elicit);

/**
 * On the client, the elicitation a server sent. A handler registered for
 * elicitation/create is given a request either way it arrived, and this
 * reads it from that.
 *
 * @throws json::JsonException when it is not an elicitation this can read.
 */
ElicitRequest fromRequest(const jsonrpc::Request& request);

/** On the client, the answer, in the form a request handler returns. */
jsonrpc::ResponseResult toResult(const ElicitResult& result);

/**
 * On the server, what the client answered: the result of the request in an
 * older revision, or the entry under inputResponses in 2026-07-28.
 *
 * @throws json::JsonException when it is not an answer to an elicitation.
 */
ElicitResult resultFrom(const json::JsonValue& answer);

/**
 * On the server, the client's response to the request toRequest built.
 *
 * @throws std::runtime_error with the client's message when it answered
 *         with an error; json::JsonException when the result is not an
 *         answer to an elicitation.
 */
ElicitResult resultFrom(const jsonrpc::Response& response);

}  // namespace elicitation
}  // namespace protocol
}  // namespace mcp
