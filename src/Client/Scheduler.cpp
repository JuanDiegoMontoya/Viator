#include "Scheduler.h"
#include "Core/Assert2.h"

#include "tracy/Tracy.hpp"
#include "ankerl/unordered_dense.h"

#include <vector>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <stack>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <format>

namespace
{
  template<typename K, typename T>
  using unordered_map = ankerl::unordered_dense::pmr::map<K, T>;

  template<typename K>
  using unordered_set = ankerl::unordered_dense::pmr::set<K>;

  template<typename Payload>
  struct DirectedAcyclicGraph
  {
    explicit DirectedAcyclicGraph(std::pmr::polymorphic_allocator<>& allocator) : allocator(&allocator), idToNode(allocator), nodes(allocator)
    {
      idToNode.reserve(100);
      nodes.reserve(100);
    }

    struct Node
    {
      explicit Node(std::pmr::polymorphic_allocator<>& allocator) : id(allocator), parents(allocator), children(allocator) {}

      std::pmr::string id;
      Payload payload;

      std::pmr::vector<Node*> parents;
      std::pmr::vector<Node*> children;
    };

    using UniquePtrToNode = std::unique_ptr<Node, decltype([](Node* p) { std::destroy_at(p); })>;

    void Insert(std::string_view id, Payload payload)
    {
      ZoneScoped;
      auto node    = DirectedAcyclicGraph::Node(*allocator);
      node.id      = std::pmr::string(id, *allocator);
      node.payload = std::move(payload);
      auto newNode = UniquePtrToNode(allocator->new_object<DirectedAcyclicGraph::Node>(std::move(node)));
      idToNode.emplace(id, newNode.get());
      nodes.push_back(std::move(newNode));
    }

    void AddDependency(std::string_view childId, std::string_view parentId)
    {
      ZoneScoped;
      auto* childNode = Find(childId);
      ASSERT(childNode);
      auto* parentNode = Find(parentId);
      ASSERT(parentNode);
      childNode->parents.push_back(parentNode);
      parentNode->children.push_back(childNode);
    }

    [[nodiscard]] Node* Find(std::string_view id)
    {
      if (auto it = idToNode.find(std::pmr::string(id, *allocator)); it != idToNode.end())
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

    [[nodiscard]] std::pmr::vector<Node*> GetTopologicallySortedNodes() const
    {
      ZoneScoped;
      auto clone      = CloneWithoutPayload();
      auto sorted     = std::pmr::vector<Node*>(*allocator);
      auto parentless = unordered_set<Node*>(*allocator);

      sorted.reserve(nodes.size());
      parentless.reserve(nodes.size());

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
      auto sortedFinal = std::pmr::vector<Node*>(*allocator);
      sortedFinal.reserve(nodes.size());
      for (auto* node : sorted)
      {
        sortedFinal.push_back(idToNode.at(node->id));
      }
      
      return sortedFinal;
    }

    std::pmr::polymorphic_allocator<>* allocator;
    unordered_map<std::pmr::string, Node*> idToNode;
    std::pmr::vector<UniquePtrToNode> nodes;
  };

  template<typename Payload>
  struct IncompleteDirectedAcyclicGraph
  {
    explicit IncompleteDirectedAcyclicGraph(std::pmr::polymorphic_allocator<>& allocator) : allocator(&allocator), nodes(allocator)
    {
      nodes.reserve(100);
    }

    struct Node
    {
      Payload payload;
      std::vector<std::pmr::string> parents;
    };

    std::pmr::polymorphic_allocator<>* allocator;
    unordered_map<std::pmr::string, Node> nodes;

    [[nodiscard]] DirectedAcyclicGraph<Payload> Complete() const
    {
      ZoneScoped;
      auto dag = DirectedAcyclicGraph<Payload>{*allocator};
      dag.nodes.reserve(nodes.size());

      for (auto& [id, node] : nodes)
      {
        dag.Insert(id, node.payload);
      }

      for (const auto& [id, node] : nodes)
      {
        auto* childNode = dag.Find(id);
        ASSERT(childNode);
        childNode->parents.reserve(node.parents.size());
        for (const auto& parentId : node.parents)
        {
          auto* parentNode = dag.Find(parentId);
          if (!parentNode)
          {
            throw std::logic_error(std::format("Parent {} with doesn't exist in the graph.", parentId));
          }
          childNode->parents.push_back(parentNode);
          parentNode->children.push_back(childNode);
        }
      }

      return dag;
    }
  };

  template<typename Payload>
  [[nodiscard]] DirectedAcyclicGraph<Payload> DirectedAcyclicGraph<Payload>::CloneWithoutPayload() const
  {
    ZoneScoped;
    auto clone = IncompleteDirectedAcyclicGraph<Payload>(*allocator);
    clone.nodes.reserve(nodes.size());

    for (const auto& node : nodes)
    {
      auto newNode = IncompleteDirectedAcyclicGraph<Payload>::Node();
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
    explicit SchedulerImpl() : graph(allocator) {}

    struct NodePayload
    {
      std::function<void()> callback;
      int priority;
    };

    void Execute(ExecuteParams params) override
    {
      ZoneScoped;
      auto completedGraph = graph.Complete();
      auto nodes          = completedGraph.GetTopologicallySortedNodes();
      auto complete       = unordered_set<const DirectedAcyclicGraph<NodePayload>::Node*>(allocator);
      auto pending        = std::pmr::vector<const DirectedAcyclicGraph<NodePayload>::Node*>(allocator);

      complete.reserve(nodes.size());
      pending.reserve(nodes.size());

      auto FlushPending = [&]
      {
        std::ranges::stable_sort(pending, std::ranges::greater{}, [](const auto& node) { return node->payload.priority; });
        for (const auto* node : pending)
        {
          if (params.nodePrologue)
          {
            params.nodePrologue(node->id.c_str());
          }
          node->payload.callback();
          if (params.nodeEpilogue)
          {
            params.nodeEpilogue();
          }

          complete.insert(node);
        }
        pending.clear();
      };

      if (params.onPassBegin)
      {
        params.onPassBegin();
      }

      for (const auto* node : nodes)
      {
        for (const auto* parent : node->parents)
        {
          if (!complete.contains(parent))
          {
            FlushPending();
            if (params.onPassEnd)
            {
              params.onPassEnd();
            }
            if (params.onPassBegin)
            {
              params.onPassBegin();
            }
            break;
          }
        }

        if (node->payload.callback)
        {
          pending.push_back(node);
        }
        else
        {
          complete.emplace(node);
        }
      }

      FlushPending();

      if (params.onPassEnd)
      {
        params.onPassEnd();
      }
    }

    std::string GenerateDotGraph() override
    {
      auto stream = std::stringstream();

      stream << "digraph Schedule {\n";

      const auto dag = graph.Complete();
      for (const auto& node : dag.nodes)
      {
        for (const auto* child : node->children)
        {
          stream << '\t' << node->id << " -> " << child->id << ";\n";
        }
      }

      stream << "}";

      return stream.str();
    }

  protected:
    void AddPassInternal(std::string_view id, int priority, std::function<void()> callback) override
    {
      ZoneScoped;
      [[maybe_unused]] auto [_, success] = graph.nodes.try_emplace(std::pmr::string(id),
        IncompleteDirectedAcyclicGraph<NodePayload>::Node{
          .payload =
            {
              .callback = std::move(callback),
              .priority = priority,
            },
        });
      DEBUG_ASSERT(success);
    }

    void AddDependency(std::string_view childId, std::string_view parentId) override
    {
      ZoneScoped;
      auto it = graph.nodes.find(std::pmr::string(childId));
      DEBUG_ASSERT(it != graph.nodes.end());
      if (it == graph.nodes.end())
      {
        return;
      }
      
      it->second.parents.emplace_back(parentId);
    }

  private:
    static constexpr int bufferSize                       = 1'000'000;
    std::unique_ptr<std::byte[]> buffer                   = std::make_unique<std::byte[]>(bufferSize);
    std::pmr::monotonic_buffer_resource arena             = {buffer.get(), bufferSize};
    std::pmr::unsynchronized_pool_resource memoryResource = std::pmr::unsynchronized_pool_resource{&arena};
    std::pmr::polymorphic_allocator<> allocator           = {&memoryResource};
    IncompleteDirectedAcyclicGraph<NodePayload> graph;
  };

  template<typename Payload>
  [[nodiscard]] bool IsTopologicallySorted(std::span<typename DirectedAcyclicGraph<Payload>::Node* const> nodes)
  {
    ZoneScoped;
    auto visited = unordered_set<const typename DirectedAcyclicGraph<Payload>::Node*>();

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
  ZoneScoped;
  return std::make_unique<SchedulerImpl>();
}


#include "doctest.h"

TEST_CASE("DirectedAcyclicGraph")
{
  ZoneScoped;
  auto allocator = std::pmr::polymorphic_allocator<>();
  auto dag = DirectedAcyclicGraph<nullptr_t>(allocator);

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
    CHECK(IsTopologicallySorted<nullptr_t>(sorted));
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
    CHECK(IsTopologicallySorted<nullptr_t>(sorted));
  }
}

TEST_CASE("Scheduler")
{
  auto scheduler = Scheduler::Create();

  SUBCASE("Simple schedule")
  {
    auto str = std::string();

    scheduler->AddPass("B", {"A"}, [&] { str += "B"; });
    scheduler->AddPass("A", [&] { str += "A"; });

    auto beginCallback = [&] { str += "begin"; };
    auto endCallback = [&] { str += "end"; };
    scheduler->Execute({.onPassBegin = beginCallback, .onPassEnd = endCallback});

    CHECK_EQ(str, "beginAendbeginBend");
  }

  SUBCASE("Ensure that a schedule with no callbacks doesn't crash")
  {
    scheduler->AddPass("A", {}, nullptr);
    scheduler->AddPass("B", {"A"}, std::function<void()>(nullptr));
    scheduler->Execute({});
  }

  SUBCASE("Ensure that a schedule with an unknown dependency throws")
  {
    scheduler->AddPass("A", {"Fake"}, nullptr);
    CHECK_THROWS(scheduler->Execute({}));
  }

  SUBCASE("Ensure that node priority is respected")
  {
    auto str = std::string();
    scheduler->AddPass("A", 0, [&] { str += "A"; });
    scheduler->AddPass("B", 1, [&] { str += "B"; });
    scheduler->AddPass("C", -1, [&] { str += "C"; });
    scheduler->Execute({});
    CHECK_EQ(str, "BAC");
  }
}