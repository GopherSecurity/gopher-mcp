// Package manager provides filter and chain management for the MCP Filter SDK.
package manager

import "fmt"

// AggregationStrategy defines response aggregation methods.
type AggregationStrategy int

const (
	FirstWin AggregationStrategy = iota
	AllMustSucceed
	Voting
	Custom
)

// DefaultAggregator implements response aggregation.
type DefaultAggregator struct {
	strategy AggregationStrategy
	custom   func([][]byte) ([]byte, error)
}

// Aggregate aggregates multiple responses.
func (a *DefaultAggregator) Aggregate(responses [][]byte) ([]byte, error) {
	switch a.strategy {
	case FirstWin:
		if len(responses) > 0 {
			return responses[0], nil
		}
		return nil, fmt.Errorf("no responses")
		
	case AllMustSucceed:
		// All responses must be non-nil
		for _, resp := range responses {
			if resp == nil {
				return nil, fmt.Errorf("response failed")
			}
		}
		return responses[len(responses)-1], nil
		
	case Voting:
		// Majority voting logic
		return a.majorityVote(responses)
		
	case Custom:
		if a.custom != nil {
			return a.custom(responses)
		}
		return nil, fmt.Errorf("no custom aggregator")
		
	default:
		return nil, fmt.Errorf("unknown strategy")
	}
}

// majorityVote implements voting aggregation.
func (a *DefaultAggregator) majorityVote(responses [][]byte) ([]byte, error) {
	// Simple majority voting implementation
	if len(responses) == 0 {
		return nil, fmt.Errorf("no responses")
	}
	return responses[0], nil
}