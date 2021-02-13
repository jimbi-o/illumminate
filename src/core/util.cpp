#include "illuminate/illuminate.h"
namespace illuminate::core {
void ConnectAdjacencyNodes(const StrId& node_name, const std::pmr::unordered_map<StrId, std::pmr::unordered_set<StrId>>& adjacency_graph, std::pmr::unordered_set<StrId>* dst, std::pmr::unordered_set<StrId>* work) {
  work->insert(node_name);
  while (!work->empty()) {
    auto pass_name = work->begin();
    if (adjacency_graph.contains(*pass_name)) {
      auto& nodes = adjacency_graph.at(*pass_name);
      work->insert(nodes.begin(), nodes.end());
    }
    dst->insert(std::move(*pass_name));
    work->erase(pass_name);
  }
}
}
