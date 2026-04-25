#pragma once
#include <functional>
#include <string_view>
#include <utility>
#include <memory>
#include <any>

class Scheduler
{
public:
  [[nodiscard]] static std::unique_ptr<Scheduler> Create();

  virtual ~Scheduler() = default;

  template<typename... Ts>
  void AddPass(std::string_view id, std::function<void()> callback, Ts... dependencies)
  {
    AddPassInternal(id, std::move(callback));
    (AddDependency(id, std::forward<Ts>(dependencies)), ...);
  }

  struct ExecuteParams
  {
    std::function<void(std::any& userData)> onPassBegin;
    std::function<void(std::any& userData)> onPassEnd;
    std::any userData;
  };

  virtual void Execute(ExecuteParams params) = 0;

protected:
  virtual void AddPassInternal(std::string_view id, std::function<void()> callback) = 0;
  virtual void AddDependency(std::string_view childId, std::string_view parentId) = 0;
};