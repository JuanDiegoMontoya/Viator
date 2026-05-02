#pragma once
#include <functional>
#include <string_view>
#include <string>
#include <utility>
#include <memory>
#include <vector>

class Scheduler
{
public:
  [[nodiscard]] static std::unique_ptr<Scheduler> Create();

  virtual ~Scheduler() = default;

  void AddPass(std::string_view id, std::function<void()> callback)
  {
    AddPassInternal(id, 0, std::move(callback));
  }

  void AddPass(std::string_view id, int priority, std::function<void()> callback)
  {
    AddPassInternal(id, priority, std::move(callback));
  }

  void AddPass(std::string_view id, const std::vector<std::string_view>& dependencies, std::function<void()> callback)
  {
    AddPass(id, dependencies, 0, std::move(callback));
  }

  void AddPass(std::string_view id, const std::vector<std::string_view>& dependencies, int priority, std::function<void()> callback)
  {
    AddPassInternal(id, priority, std::move(callback));
    for (auto dependency : dependencies)
    {
      AddDependency(id, dependency);
    }
  }

  struct ExecuteParams
  {
    std::function<void(const char* nodeId)> nodePrologue;
    std::function<void()> nodeEpilogue;
    std::function<void()> onPassBegin;
    std::function<void()> onPassEnd;
  };

  virtual void Execute(ExecuteParams params) = 0;

  [[nodiscard]] virtual std::string GenerateDotGraph() = 0;

protected:
  virtual void AddPassInternal(std::string_view id, int priority, std::function<void()> callback) = 0;
  virtual void AddDependency(std::string_view childId, std::string_view parentId) = 0;
};