#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

  ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data)) {
  }

  void ObjectHolder::AssertIsValid() const {
    assert(data_ != nullptr);
  }

  ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
  }

  ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
  }

  Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
  }

  Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
  }

  Object* ObjectHolder::Get() const {
    return data_.get();
  }

  ObjectHolder::operator bool() const {
    return Get() != nullptr;
  }

  bool IsTrue(const ObjectHolder& object) {
    if (!object) {
      return false;
    }

    if (const Bool* boolean = object.TryAs<Bool>(); boolean) {
      return boolean->GetValue();
    }

    if (const Number* number = object.TryAs<Number>(); number) {
      return number->GetValue() != 0;
    }

    if (const String* string = object.TryAs<String>(); string) {
      return !string->GetValue().empty();
    }

    return false;
  }

  void ClassInstance::Print(std::ostream& os, Context& context) {
    static const std::string STR = "__str__";
    static int STR_METHOD_PARAMS_COUNT = 0;

    if (HasMethod(STR, STR_METHOD_PARAMS_COUNT)) {
      Call(STR, {}, context)->Print(os, context);
    }
    else {
      os << this;
    }
  }

  bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
    const Method* method_ptr = class_.GetMethod(method);

    return method_ptr && argument_count == method_ptr->formal_params.size();
  }

  Closure& ClassInstance::Fields() {
    return fields_;
  }

  const Closure& ClassInstance::Fields() const {
    return fields_;
  }

  ClassInstance::ClassInstance(const Class& cls)
    : class_(cls)
  {}

  ObjectHolder ClassInstance::Call(
    const std::string& method_name,
    const std::vector<ObjectHolder>& actual_args,
    Context& context
  ) {
    
    if (!HasMethod(method_name, actual_args.size())) {
      std::stringstream err_builder;
      err_builder << "Method "s
        << method_name
        << " is not implemented."s;

      throw std::runtime_error(err_builder.str());
    }

    Closure closure = {
      {"self"s, ObjectHolder::Share(*this) }
    };

    const Method* method = class_.GetMethod(method_name);
    size_t expect_params_count = method->formal_params.size();

    if (expect_params_count > actual_args.size()) {
      std::stringstream err_builder;
      err_builder
        << expect_params_count
        << " arguments were expected for the "s
        << method_name
        << " function."s;
      throw std::runtime_error(err_builder.str());
    }

    for (size_t i = 0; i < method->formal_params.size(); i++) {
      closure[method->formal_params[i]] = actual_args[i];
    }
    
    return method->body->Execute(closure, context);
  }

  Class::Class(
    std::string name,
    std::vector<Method> methods,
    const Class* parent
  )
    : name_(name)
    , methods_(std::forward<vector<Method>>(methods))
    , parent_(parent)
  {
    for (const Method& method : methods_) {
      table_methods_[method.name] = &method;
    }
  }

  const Method* Class::GetMethod(const std::string& name) const {
    if (table_methods_.count(name)) {
      return table_methods_.at(name);
    }

    if (parent_) {
      return parent_->GetMethod(name);
    }

    return nullptr;
  }

  [[nodiscard]] const std::string& Class::GetName() const {
    if (name_.empty()) {
      throw std::runtime_error("Class name must not be empty"s);
    }

    return name_;
  }

  void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
    os << "Class "s << name_;
  }

  void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? "True"sv : "False"sv);
  }

  template<typename T>
  std::pair<T*, T*> TryAsPair(const ObjectHolder& lhs, const ObjectHolder& rhs) {
    return { lhs.TryAs<T>(), rhs.TryAs<T>() };
  }

  template<typename Comparator>
  bool ComparatorImpl(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context,
    Comparator comparator,
    const std::string& default_method_name,
    int default_comparator_params_count = 1

  ) {
    if (const auto& [lhs_bool, rhs_bool] = TryAsPair<Bool>(lhs, rhs); lhs_bool && rhs_bool) {
      return comparator(lhs_bool->GetValue(), rhs_bool->GetValue());
    }

    if (const auto& [lhs_num, rhs_num] = TryAsPair<Number>(lhs, rhs); lhs_num && rhs_num) {
      return comparator(lhs_num->GetValue(), rhs_num->GetValue());
    }

    if (const auto& [lhs_str, rhs_str] = TryAsPair<String>(lhs, rhs); lhs_str && rhs_str) {
      return comparator(lhs_str->GetValue(), rhs_str->GetValue());
    }

    if (
      const auto& class_instance = lhs.TryAs<ClassInstance>();
      class_instance && class_instance->HasMethod(
        default_method_name,
        default_comparator_params_count
      )
    ) {
      return class_instance->Call(default_method_name, { rhs }, context).TryAs<Bool>()->GetValue();
    }

    throw std::runtime_error("Non-comparable objects"s);
  }

  bool Equal(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    static const std::string EQ = "__eq__";

    if (!lhs && !rhs) {
      return true;
    }

    auto eq_comparator = [](const auto& lhs, const auto& rhs) -> bool {
      return lhs == rhs;
    };

    return ComparatorImpl(
      lhs,
      rhs,
      context,
      eq_comparator,
      EQ
    );
  }

  bool Less(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    static const std::string lt = "__lt__"s;

    auto less_comparator = [](const auto& lhs, const auto& rhs) -> bool {
      return lhs < rhs;
    };

    return ComparatorImpl(
      lhs,
      rhs,
      context,
      less_comparator,
      lt
    );
  }

  bool NotEqual(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    return !Equal(lhs, rhs, context);
  }

  bool Greater(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    return !Less(lhs, rhs, context) && NotEqual(lhs, rhs, context);
  }

  bool LessOrEqual(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    return Less(lhs, rhs, context) || Equal(lhs, rhs, context);
  }

  bool GreaterOrEqual(
    const ObjectHolder& lhs,
    const ObjectHolder& rhs,
    Context& context
  ) {
    return !Less(lhs, rhs, context);
  }

}  // namespace runtime