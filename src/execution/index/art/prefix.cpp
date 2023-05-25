#include "duckdb/execution/index/art/prefix.hpp"

#include "duckdb/execution/index/art/art.hpp"
#include "duckdb/execution/index/art/art_key.hpp"
#include "duckdb/execution/index/art/node.hpp"
#include "duckdb/storage/meta_block_reader.hpp"
#include "duckdb/storage/meta_block_writer.hpp"

namespace duckdb {

Prefix &Prefix::New(ART &art, Node &node) {

	node.SetPtr(Node::GetAllocator(art, NType::PREFIX).New());
	node.type = (uint8_t)NType::PREFIX;

	auto &prefix = Prefix::Get(art, node);
	prefix.data[Node::PREFIX_SIZE] = 0;
	return prefix;
}

Prefix &Prefix::New(ART &art, Node &node, uint8_t byte, Node next) {

	node.SetPtr(Node::GetAllocator(art, NType::PREFIX).New());
	node.type = (uint8_t)NType::PREFIX;

	auto &prefix = Prefix::Get(art, node);

	// set fields
	prefix.data[Node::PREFIX_SIZE] = 1;
	prefix.data[0] = byte;
	prefix.ptr = next;
	return prefix;
}

void Prefix::New(ART &art, reference<Node> &node, const ARTKey &key, const uint32_t depth, uint32_t count) {

	if (count == 0) {
		return;
	}
	idx_t copy_count = 0;

	while (count) {
		node.get().SetPtr(Node::GetAllocator(art, NType::PREFIX).New());
		node.get().type = (uint8_t)NType::PREFIX;
		auto &prefix = Prefix::Get(art, node);

		auto this_count = MinValue((uint32_t)Node::PREFIX_SIZE, count);
		prefix.data[Node::PREFIX_SIZE] = (uint8_t)this_count;
		memcpy(prefix.data, key.data + depth + copy_count, this_count);

		node = prefix.ptr;
		copy_count += this_count;
		count -= this_count;
	}
}

void Prefix::Free(ART &art, Node &node) {

	D_ASSERT(node.IsSet());
	D_ASSERT(!node.IsSwizzled());

	// free child node
	auto &child = Prefix::Get(art, node).ptr;
	Node::Free(art, child);
}

void Prefix::Concatenate(ART &art, Node &prefix_node, const uint8_t byte, Node &child_prefix_node) {

	D_ASSERT(prefix_node.IsSet() && !prefix_node.IsSwizzled());
	D_ASSERT(child_prefix_node.IsSet() && !child_prefix_node.IsSwizzled());

	// append a byte and a child_prefix to prefix
	if (prefix_node.DecodeARTNodeType() == NType::PREFIX) {

		// get the tail
		reference<Prefix> prefix = Prefix::Get(art, prefix_node);
		D_ASSERT(prefix.get().ptr.IsSet());
		if (prefix.get().ptr.IsSwizzled()) {
			prefix.get().ptr.Deserialize(art);
		}
		while (prefix.get().ptr.DecodeARTNodeType() == NType::PREFIX) {
			prefix = Prefix::Get(art, prefix.get().ptr);
			D_ASSERT(prefix.get().ptr.IsSet());
			if (prefix.get().ptr.IsSwizzled()) {
				prefix.get().ptr.Deserialize(art);
			}
		}

		// append the byte
		prefix = prefix.get().Append(art, byte);

		if (child_prefix_node.DecodeARTNodeType() == NType::PREFIX) {
			// append the child prefix
			prefix.get().Append(art, child_prefix_node);
		} else {
			// set child_prefix_node to succeed prefix
			prefix.get().ptr = child_prefix_node;
		}
		return;
	}

	// create a new prefix node containing byte, then append the child_prefix to it
	if (prefix_node.DecodeARTNodeType() != NType::PREFIX && child_prefix_node.DecodeARTNodeType() == NType::PREFIX) {

		auto child_prefix = child_prefix_node;
		auto &prefix = Prefix::New(art, prefix_node, byte, Node());

		prefix.Append(art, child_prefix);
		return;
	}

	// neither prefix nor child_prefix are prefix nodes
	Prefix::New(art, prefix_node, byte, child_prefix_node);
}

idx_t Prefix::Traverse(ART &art, reference<Node> &l_node, reference<Node> &r_node) {

	D_ASSERT(l_node.get().IsSet() && !l_node.get().IsSwizzled());
	D_ASSERT(r_node.get().IsSet() && !r_node.get().IsSwizzled());

	D_ASSERT(l_node.get().DecodeARTNodeType() == NType::PREFIX);
	D_ASSERT(r_node.get().DecodeARTNodeType() == NType::PREFIX);

	vector<reference<Node>> traversed_l_nodes;
	vector<reference<Node>> traversed_r_nodes;

	while (l_node.get().DecodeARTNodeType() == NType::PREFIX && r_node.get().DecodeARTNodeType() == NType::PREFIX) {

		auto &l_prefix = Prefix::Get(art, l_node);
		auto &r_prefix = Prefix::Get(art, r_node);

		// compare prefix bytes
		auto max_count = MinValue(l_prefix.data[Node::PREFIX_SIZE], r_prefix.data[Node::PREFIX_SIZE]);
		for (idx_t i = 0; i < max_count; i++) {
			if (l_prefix.data[i] != r_prefix.data[i]) {
				// mismatching byte position
				return i;
			}
		}

		// prefixes match (so far)
		if (l_prefix.data[Node::PREFIX_SIZE] == r_prefix.data[Node::PREFIX_SIZE]) {
			traversed_l_nodes.push_back(l_node);
			traversed_r_nodes.push_back(r_node);
			D_ASSERT(l_prefix.ptr.IsSet() && !l_prefix.ptr.IsSwizzled());
			D_ASSERT(r_prefix.ptr.IsSet() && !r_prefix.ptr.IsSwizzled());
			l_node = l_prefix.ptr;
			r_node = r_prefix.ptr;
			continue;
		}

		// r_prefix contains l_prefix
		if (l_prefix.data[Node::PREFIX_SIZE] == max_count) {
			// free preceding r_prefix nodes
			if (!traversed_r_nodes.empty()) {
				auto &prev_prefix = Prefix::Get(art, traversed_r_nodes.back());
				prev_prefix.ptr.Reset();
				Node::Free(art, traversed_r_nodes.front());
			}
			D_ASSERT(l_prefix.ptr.IsSet() && !l_prefix.ptr.IsSwizzled());
			l_node = l_prefix.ptr;
			return max_count;
		}
		// l_prefix contains r_prefix
		if (r_prefix.data[Node::PREFIX_SIZE] == max_count) {
			// free preceding l_prefix nodes
			if (!traversed_l_nodes.empty()) {
				auto &prev_prefix = Prefix::Get(art, traversed_l_nodes.back());
				prev_prefix.ptr.Reset();
				Node::Free(art, traversed_l_nodes.front());
			}
			D_ASSERT(r_prefix.ptr.IsSet() && !r_prefix.ptr.IsSwizzled());
			r_node = r_prefix.ptr;
			return max_count;
		}
	}

	// prefixes match
	D_ASSERT(l_node.get().DecodeARTNodeType() != NType::PREFIX);
	D_ASSERT(r_node.get().DecodeARTNodeType() != NType::PREFIX);
	return DConstants::INVALID_INDEX;
}

idx_t Prefix::Traverse(ART &art, reference<Node> &prefix_node, const ARTKey &key, idx_t &depth) {

	D_ASSERT(prefix_node.get().IsSet() && !prefix_node.get().IsSwizzled());
	D_ASSERT(prefix_node.get().DecodeARTNodeType() == NType::PREFIX);

	// compare prefix nodes to key bytes
	while (prefix_node.get().DecodeARTNodeType() == NType::PREFIX) {
		auto &prefix = Prefix::Get(art, prefix_node);
		for (idx_t i = 0; i < prefix.data[Node::PREFIX_SIZE]; i++) {
			if (prefix.data[i] != key[depth]) {
				return i;
			}
			depth++;
		}
		prefix_node = prefix.ptr;
		D_ASSERT(prefix_node.get().IsSet());
		if (prefix_node.get().IsSwizzled()) {
			prefix_node.get().Deserialize(art);
		}
	}

	return DConstants::INVALID_INDEX;
}

void Prefix::Reduce(ART &art, Node &prefix_node, const idx_t n) {

	D_ASSERT(prefix_node.IsSet() && !prefix_node.IsSwizzled());
	D_ASSERT(n < Node::PREFIX_SIZE);

	reference<Prefix> prefix = Prefix::Get(art, prefix_node);

	// free this prefix node
	if (n == Node::PREFIX_SIZE - 1) {
		auto next_ptr = prefix.get().ptr;
		D_ASSERT(next_ptr.IsSet());
		prefix.get().ptr.Reset();
		Node::Free(art, prefix_node);
		prefix_node = next_ptr;
		return;
	}

	// shift by n bytes in the current prefix
	for (idx_t i = 0; i < Node::PREFIX_SIZE - n - 1; i++) {
		prefix.get().data[i] = prefix.get().data[n + i + 1];
	}
	prefix.get().data[Node::PREFIX_SIZE] = Node::PREFIX_SIZE - n - 1;

	// append the remaining prefix bytes
	prefix.get().Append(art, prefix.get().ptr);
}

void Prefix::Split(ART &art, reference<Node> &prefix_node, Node &child_node, idx_t position) {

	D_ASSERT(prefix_node.get().IsSet() && !prefix_node.get().IsSwizzled());

	auto &prefix = Prefix::Get(art, prefix_node);

	// the split is at the last byte of this prefix, so the child_node contains all subsequent
	// prefix nodes (prefix.ptr) (if any), and the count of this prefix decreases by one,
	// then, we reference prefix.ptr, to overwrite is with a new node later
	if (position + 1 == Node::PREFIX_SIZE) {
		prefix.data[Node::PREFIX_SIZE]--;
		prefix_node = prefix.ptr;
		child_node = prefix.ptr;
		return;
	}

	// append the remaining bytes after the split
	if (position + 1 < prefix.data[Node::PREFIX_SIZE]) {
		reference<Prefix> child_prefix = Prefix::New(art, child_node);
		for (idx_t i = position + 1; i < prefix.data[Node::PREFIX_SIZE]; i++) {
			child_prefix = child_prefix.get().Append(art, prefix.data[i]);
		}

		if (prefix.ptr.DecodeARTNodeType() == NType::PREFIX) {
			child_prefix.get().Append(art, prefix.ptr);
		} else {
			// this is the last prefix node of the prefix
			child_prefix.get().ptr = prefix.ptr;
		}
	}

	// this is the last prefix node of the prefix
	if (position + 1 == prefix.data[Node::PREFIX_SIZE]) {
		child_node = prefix.ptr;
	}

	// set the new size of this node
	prefix.data[Node::PREFIX_SIZE] = position;

	// no bytes left before the split, free this node
	if (position == 0) {
		prefix.ptr.Reset();
		Node::Free(art, prefix_node.get());
		return;
	}

	// bytes left before the split, reference subsequent node
	prefix_node = prefix.ptr;
	return;
}

string Prefix::ToString(ART &art) {

	D_ASSERT(data[Node::PREFIX_SIZE] != 0);
	D_ASSERT(data[Node::PREFIX_SIZE] <= Node::PREFIX_SIZE);

	string str = " prefix_bytes:[";
	for (idx_t i = 0; i < data[Node::PREFIX_SIZE]; i++) {
		str += to_string(data[i]) + "-";
	}
	str += "] ";

	return str + ptr.ToString(art);
}

BlockPointer Prefix::Serialize(ART &art, MetaBlockWriter &writer) {

	// recurse into the child and retrieve its block pointer
	auto child_block_pointer = ptr.Serialize(art, writer);

	// get pointer and write fields
	auto block_pointer = writer.GetBlockPointer();
	writer.Write(NType::PREFIX);
	writer.Write<uint8_t>(data[Node::PREFIX_SIZE]);

	// write prefix bytes
	for (idx_t i = 0; i < data[Node::PREFIX_SIZE]; i++) {
		writer.Write(data[i]);
	}

	// write child block pointer
	writer.Write(child_block_pointer.block_id);
	writer.Write(child_block_pointer.offset);

	return block_pointer;
}

void Prefix::Deserialize(MetaBlockReader &reader) {

	data[Node::PREFIX_SIZE] = reader.Read<uint8_t>();

	// read bytes
	for (idx_t i = 0; i < data[Node::PREFIX_SIZE]; i++) {
		data[i] = reader.Read<uint8_t>();
	}

	// read child block pointer
	ptr = Node(reader);
}

Prefix &Prefix::Append(ART &art, const uint8_t byte) {

	reference<Prefix> prefix(*this);

	// we need a new prefix node
	if (prefix.get().data[Node::PREFIX_SIZE] == Node::PREFIX_SIZE) {
		prefix = Prefix::New(art, prefix.get().ptr);
	}

	prefix.get().data[prefix.get().data[Node::PREFIX_SIZE]] = byte;
	prefix.get().data[Node::PREFIX_SIZE]++;
	return prefix.get();
}

void Prefix::Append(ART &art, Node other_prefix) {

	D_ASSERT(other_prefix.IsSet());
	if (other_prefix.IsSwizzled()) {
		other_prefix.Deserialize(art);
	}

	reference<Prefix> prefix(*this);
	while (other_prefix.DecodeARTNodeType() == NType::PREFIX) {

		// copy prefix bytes
		auto &other = Prefix::Get(art, other_prefix);
		for (idx_t i = 0; i < other.data[Node::PREFIX_SIZE]; i++) {
			prefix = prefix.get().Append(art, other.data[i]);
		}

		D_ASSERT(other.ptr.IsSet());
		if (other.ptr.IsSwizzled()) {
			other.ptr.Deserialize(art);
		}

		prefix.get().ptr = other.ptr;
		Node::GetAllocator(art, NType::PREFIX).Free(other_prefix);
		other_prefix = prefix.get().ptr;
	}

	D_ASSERT(prefix.get().ptr.DecodeARTNodeType() != NType::PREFIX);
}

} // namespace duckdb
