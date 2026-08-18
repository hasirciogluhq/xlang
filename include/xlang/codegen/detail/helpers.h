#pragma once

#include "xlang/ast.h"
#include "xlang/types.h"

#include <optional>
#include <string>
#include <vector>

namespace xlang::codegen_detail {

[[nodiscard]] std::vector<Type> paramTypes(const std::vector<TypedName>& params);
[[nodiscard]] bool paramTypesMatch(const std::vector<Type>& expected,
                                   const std::vector<Type>& actual, bool variadic);
[[nodiscard]] bool paramTypesMatchWithWidening(const std::vector<Type>& expected,
                                               const std::vector<Type>& actual, bool variadic);
[[nodiscard]] std::optional<FunctionSignature> findMatchingFunction(
    const std::string& name, const std::vector<Type>& arg_types,
    const std::vector<FunctionSignature>& candidates);
[[nodiscard]] std::vector<FunctionSignature> collectDefinedFunctions(const Program& program);
[[nodiscard]] const Function* findFunctionDefinition(const Program& program, const std::string& name,
                                                     const std::vector<Type>& param_types);
[[nodiscard]] const Function* findUniqueFunctionByName(const Program& program, const std::string& name);
[[nodiscard]] bool programUsesStrings(const Program& program);
[[nodiscard]] bool programUsesHeap(const Program& program);

}  // namespace xlang::codegen_detail
