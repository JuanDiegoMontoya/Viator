#pragma once
#include <functional>
#include <string_view>
#include <utility>
#include <memory>

class Scheduler
{
public:
  static std::unique_ptr<Scheduler> Create();

  virtual ~Scheduler() = default;

  template<typename... Ts>
  void AddPass(std::string_view id, std::function<void()> callback, Ts... dependencies)
  {
    AddPass(id, std::move(callback));
    (AddDependency(id, std::forward<Ts>(dependencies)), ...);
  }

  virtual void Execute() = 0;

protected:
  virtual void AddPass(std::string_view id, std::function<void()> callback) = 0;
  virtual void AddDependency(std::string_view childId, std::string_view parentId) = 0;
};