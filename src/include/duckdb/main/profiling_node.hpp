//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/main/profiling_node.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/deque.hpp"
#include "duckdb/common/enums/profiler_format.hpp"
#include "duckdb/common/pair.hpp"
#include "duckdb/common/profiler.hpp"
#include "duckdb/common/reference_map.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/winapi.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/main/profiling_info.hpp"

namespace duckdb {

enum class ProfilingNodeType : uint8_t { QUERY_ROOT, OPERATOR };

class QueryProfilingNode;

struct QueryInfo {
	QueryInfo() : idle_thread_time(0) {};
	string query_name;
	double idle_thread_time;
};

// Recursive tree that mirrors the operator tree
class ProfilingNode {
public:
	explicit ProfilingNode(ProfilingNodeType node_type) : node_type(node_type) {
	}
	virtual ~ProfilingNode() {};

private:
	ProfilingInfo profiling_info;

public:
	idx_t depth = 0;
	vector<unique_ptr<ProfilingNode>> children;
	ProfilingNodeType node_type = ProfilingNodeType::OPERATOR;

public:
	virtual string GetName() const = 0;

	idx_t GetChildCount() {
		return children.size();
	}

	ProfilingInfo &GetProfilingInfo() {
		return profiling_info;
	}

	const ProfilingInfo &GetProfilingInfo() const {
		return profiling_info;
	}

	optional_ptr<ProfilingNode> GetChild(idx_t idx) {
		return children[idx].get();
	}

	optional_ptr<ProfilingNode> AddChild(unique_ptr<ProfilingNode> child) {
		children.push_back(std::move(child));
		return children.back().get();
	}

	template <class TARGET>
	TARGET &Cast() {
		if (node_type != TARGET::TYPE) {
			throw InternalException("Failed to cast ProfilingNode - node type mismatch");
		}
		return reinterpret_cast<TARGET &>(*this);
	}

	template <class TARGET>
	const TARGET &Cast() const {
		if (node_type != TARGET::TYPE) {
			throw InternalException("Failed to cast ProfilingNode - node type mismatch");
		}
		return reinterpret_cast<const TARGET &>(*this);
	}
};

// Holds the top level query info
class QueryProfilingNode : public ProfilingNode {
public:
	static constexpr const ProfilingNodeType TYPE = ProfilingNodeType::QUERY_ROOT;

public:
	explicit QueryProfilingNode(const QueryInfo &query_info) : ProfilingNode(TYPE), query_info(query_info) {
	}
	~QueryProfilingNode() override {};

public:
	string GetName() const override {
		return EnumUtil::ToString(node_type);
	}
	QueryInfo query_info;
};

class OperatorProfilingNode : public ProfilingNode {
public:
	static constexpr const ProfilingNodeType TYPE = ProfilingNodeType::OPERATOR;

public:
	OperatorProfilingNode(const string &name, PhysicalOperatorType type) : ProfilingNode(TYPE), name(name), type(type) {
	}
	~OperatorProfilingNode() override {};

public:
	string GetName() const override {
		return name;
	}

public:
	string name;
	PhysicalOperatorType type;
};

} // namespace duckdb
