#include "Scheduler.h"

#include "Core/Assert2.h"

#include <vector>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <stack>
#include <ranges>

namespace
{
  struct DirectedAcyclicGraph
  {
    struct Node
    {
      std::string id;
      std::function<void()> payload;

      std::vector<Node*> parents;
      std::vector<Node*> children;
    };

    void Insert(std::string_view id, std::function<void()> payload)
    {
      auto newNode = std::make_unique<DirectedAcyclicGraph::Node>(DirectedAcyclicGraph::Node{.id = std::string(id), .payload = std::move(payload)});
      idToNode.emplace(id, newNode.get());
      nodes.push_back(std::move(newNode));
    }

    void AddDependency(std::string_view childId, std::string_view parentId)
    {
      auto* childNode = Find(childId);
      ASSERT(childNode);
      auto* parentNode = Find(parentId);
      ASSERT(parentNode);
      childNode->parents.push_back(parentNode);
      parentNode->children.push_back(childNode);
    }

    [[nodiscard]] Node* Find(std::string_view id)
    {
      if (auto it = idToNode.find(std::string(id)); it != idToNode.end())
      {
        return it->second;
      }

      return nullptr;
    }

    [[nodiscard]] bool IsAcyclic() const
    {
      for (const auto& node : nodes)
      {
        auto visited = std::unordered_set<Node*>();
        auto toVisit = std::stack<std::pair<int, Node*>>();
        
        toVisit.emplace(0, node.get());

        int prevDepth = 0;
        Node* prevNode = nullptr;

        while (!toVisit.empty())
        {
          auto [depth, pNode] = toVisit.top();
          toVisit.pop();
          
          if (depth < prevDepth)
          {
            visited.erase(prevNode);
          }

          if (auto [_, success] = visited.emplace(pNode); !success)
          {
            return false;
          }

          for (auto child : pNode->children)
          {
            toVisit.emplace(depth + 1, child);
          }

          prevDepth = depth;
          prevNode  = pNode;
        }
      }

      return true;
    }

    [[nodiscard]] DirectedAcyclicGraph CloneWithoutPayload() const;

    [[nodiscard]] std::vector<Node*> GetTopologicallySortedNodes() const
    {
      auto clone      = CloneWithoutPayload();
      auto sorted     = std::vector<Node*>();
      auto parentless = std::unordered_set<Node*>();

      sorted.reserve(nodes.size());

      for (auto& node : clone.nodes)
      {
        if (node->parents.empty())
        {
          parentless.insert(node.get());
        }
      }

      // Kahn's algorithm.
      while (!parentless.empty())
      {
        auto* pNode = *parentless.begin();
        parentless.erase(pNode);
        sorted.push_back(pNode);

        while (!pNode->children.empty())
        {
          auto* child = pNode->children.back();
          
          // Slightly stinky O(n) lookup.
          auto it = std::ranges::find(child->parents, pNode);
          DEBUG_ASSERT(it != child->parents.end());
          std::swap(*it, child->parents.back());
          child->parents.pop_back();

          pNode->children.pop_back();

          if (child->parents.empty())
          {
            parentless.insert(child);
          }
        }
      }

      // Convert node pointers from the clone graph to pointers to actual graph.
      auto sortedFinal = std::vector<Node*>();
      sortedFinal.reserve(nodes.size());
      for (auto* node : sorted)
      {
        sortedFinal.push_back(idToNode.at(node->id));
      }

      return sortedFinal;
    }

    std::unordered_map<std::string, Node*> idToNode;
    std::vector<std::unique_ptr<Node>> nodes;
  };

  struct IncompleteDirectedAcyclicGraph
  {
    struct Node
    {
      std::function<void()> payload;
      std::vector<std::string> parents;
    };

    std::unordered_map<std::string, Node> nodes;

    [[nodiscard]] DirectedAcyclicGraph Complete()
    {
      auto dag = DirectedAcyclicGraph{};
      dag.nodes.reserve(nodes.size());

      for (auto& [id, node] : nodes)
      {
        dag.Insert(id, std::move(node.payload));
      }

      for (const auto& [id, node] : nodes)
      {
        auto* childNode = dag.Find(id);
        ASSERT(childNode);
        childNode->parents.reserve(node.parents.size());
        for (const auto& parentId : node.parents)
        {
          auto* parentNode = dag.Find(parentId);
          ASSERT(parentNode);
          childNode->parents.push_back(parentNode);
          parentNode->children.push_back(childNode);
        }
      }

      nodes.clear();
      return dag;
    }
  };

  DirectedAcyclicGraph DirectedAcyclicGraph::CloneWithoutPayload() const
  {
    auto clone = IncompleteDirectedAcyclicGraph();
    clone.nodes.reserve(nodes.size());

    for (const auto& node : nodes)
    {
      auto newNode = IncompleteDirectedAcyclicGraph::Node();
      newNode.parents.reserve(node->parents.size());

      for (const auto* parent : node->parents)
      {
        newNode.parents.push_back(parent->id);
      }

      clone.nodes.try_emplace(node->id, newNode);
    }

    return clone.Complete();
  }

  class SchedulerImpl : public Scheduler
  {
  public:
    void Execute() override
    {
      auto nodes = graph.Complete().GetTopologicallySortedNodes();
      for (const auto& node : nodes)
      {
        node->payload();
      }
    }

  protected:
    void AddPass(std::string_view id, std::function<void()> callback) override
    {
      [[maybe_unused]] auto [_, success] = graph.nodes.try_emplace(std::string(id), IncompleteDirectedAcyclicGraph::Node{.payload = std::move(callback)});
      DEBUG_ASSERT(success);
    }

    void AddDependency(std::string_view childId, std::string_view parentId) override
    {
      auto it = graph.nodes.find(std::string(childId));
      DEBUG_ASSERT(it != graph.nodes.end());
      if (it == graph.nodes.end())
      {
        return;
      }
      
      it->second.parents.emplace_back(parentId);
    }

  private:
    IncompleteDirectedAcyclicGraph graph;
  };

  bool IsTopologicallySorted(std::span<DirectedAcyclicGraph::Node* const> nodes)
  {
    auto visited = std::unordered_set<const DirectedAcyclicGraph::Node*>();

    for (auto* node : nodes)
    {
      visited.emplace(node);

      for (auto* child : node->children)
      {
        if (visited.contains(child))
        {
          return false;
        }
      }
    }

    return true;
  }
} // namespace

std::unique_ptr<Scheduler> Scheduler::Create()
{
  return std::make_unique<SchedulerImpl>();
}


#include "doctest.h"

TEST_CASE("DirectedAcyclicGraph")
{
  auto dag = DirectedAcyclicGraph();

  SUBCASE("Basic cycle detection")
  {
    dag.Insert("A", nullptr);
    CHECK(dag.IsAcyclic());
    dag.Insert("B", nullptr);
    CHECK(dag.IsAcyclic());
    dag.AddDependency("B", "A");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("A", "B");
    CHECK(!dag.IsAcyclic());
  }

  SUBCASE("Loop cycle detection")
  {
    dag.Insert("A", nullptr);
    dag.AddDependency("A", "A");
    CHECK(!dag.IsAcyclic());
  }

  SUBCASE("Transitive cycle detection")
  {
    dag.Insert("A", nullptr);
    dag.Insert("B", nullptr);
    dag.Insert("C", nullptr);
    dag.AddDependency("C", "B");
    dag.AddDependency("B", "A");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("A", "C");
    CHECK(!dag.IsAcyclic());
  }

  SUBCASE("Cycle detection with multiple parents")
  {
    dag.Insert("A0", nullptr);
    dag.Insert("A1", nullptr);
    dag.Insert("B0", nullptr);
    dag.Insert("B1", nullptr);
    dag.Insert("C", nullptr);
    dag.AddDependency("C", "B0");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("B0", "A0");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("B0", "A1");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("B1", "A0");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("B1", "A1");
    CHECK(dag.IsAcyclic());
    dag.AddDependency("A1", "C");
    CHECK(!dag.IsAcyclic());
  }

  SUBCASE("Simple topological sort")
  {
    dag.Insert("B", nullptr);
    dag.Insert("A", nullptr);
    dag.AddDependency("B", "A");
    auto sorted = dag.GetTopologicallySortedNodes();
    REQUIRE(sorted.size() == 2);
    CHECK(sorted[0] == dag.idToNode.at("A"));
    CHECK(sorted[1] == dag.idToNode.at("B"));
    CHECK(IsTopologicallySorted(sorted));
  }

  SUBCASE("Advanced topological sort")
  {
    // https://en.wikipedia.org/wiki/Topological_sorting#/media/File:Directed_acyclic_graph_2.svg
    dag.Insert("5", nullptr);
    dag.Insert("7", nullptr);
    dag.Insert("3", nullptr);
    dag.Insert("11", nullptr);
    dag.Insert("8", nullptr);
    dag.Insert("2", nullptr);
    dag.Insert("9", nullptr);
    dag.Insert("10", nullptr);
    dag.AddDependency("11", "5");
    dag.AddDependency("11", "7");
    dag.AddDependency("8", "7");
    dag.AddDependency("8", "3");
    dag.AddDependency("2", "11");
    dag.AddDependency("9", "11");
    dag.AddDependency("9", "8");
    dag.AddDependency("10", "11");
    dag.AddDependency("10", "3");
    auto sorted = dag.GetTopologicallySortedNodes();
    CHECK(sorted.size() == dag.nodes.size());
    CHECK(IsTopologicallySorted(sorted));
  }
}