#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace caldera::backend::processing {

struct StageSpec {
    std::string name; // canonical lowercase identifier
    std::unordered_map<std::string,std::string> params; // key->value (already trimmed)
};

struct PipelineParseResult {
    std::vector<StageSpec> stages; // empty if error
    bool ok = false;               // false if any fatal parse error
    std::string error;             // description (first error encountered)
};

// Parse CALDERA_PIPELINE grammar:
//   STAGE ("," STAGE)*
//   STAGE := IDENT [ "(" PARAM_LIST ")" ]
//   PARAM_LIST := PARAM ("," PARAM)*
//   PARAM := KEY "=" VALUE
// Whitespace ignored around tokens. IDENT/KEY are [A-Za-z0-9_]+ . VALUE is until next comma or ')' (trimmed).
PipelineParseResult parsePipelineSpec(const std::string& spec);

} // namespace caldera::backend::processing
