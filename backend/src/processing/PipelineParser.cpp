#include "processing/PipelineParser.h"
#include <cctype>
#include <sstream>

namespace caldera::backend::processing {

namespace {
static inline void ltrim(std::string& s){ size_t i=0; while(i<s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; if(i) s.erase(0,i); }
static inline void rtrim(std::string& s){ size_t i=s.size(); while(i>0 && std::isspace(static_cast<unsigned char>(s[i-1]))) --i; if(i<s.size()) s.erase(i); }
static inline void trim(std::string& s){ ltrim(s); rtrim(s); }
static bool isIdentChar(char c){ return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='-'; }
}

PipelineParseResult parsePipelineSpec(const std::string& spec) {
    PipelineParseResult result; result.ok=true;
    std::string current;
    // We manually scan to split top-level commas (not inside parentheses)
    std::vector<std::string> stageRaw;
    int parenDepth=0; std::string token;
    for(size_t i=0;i<spec.size();++i){
        char c=spec[i];
        if(c=='(') { ++parenDepth; token.push_back(c); }
        else if(c==')'){ if(parenDepth>0) --parenDepth; token.push_back(c); }
        else if(c==',' && parenDepth==0){ trim(token); if(!token.empty()) stageRaw.push_back(token); token.clear(); }
        else token.push_back(c);
    }
    trim(token); if(!token.empty()) stageRaw.push_back(token);
    if(stageRaw.empty()) { result.ok=false; result.error="empty pipeline spec"; return result; }

    for(auto& sr : stageRaw){
        StageSpec specOut; // parse name and optional params block
        // Find '(' if any
        size_t lp = sr.find('(');
        std::string head = sr;
        std::string paramBlock;
        if(lp != std::string::npos){
            size_t rp = sr.rfind(')');
            if(rp == std::string::npos || rp < lp){
                result.ok=false; result.error="unmatched '(' in stage: "+sr; result.stages.clear(); return result; }
            head = sr.substr(0, lp);
            paramBlock = sr.substr(lp+1, rp - (lp+1));
        }
        trim(head);
        if(head.empty()) { result.ok=false; result.error="missing stage identifier in segment: "+sr; result.stages.clear(); return result; }
        // Validate ident
        for(char c: head){ if(!isIdentChar(c)){ result.ok=false; result.error="invalid char in stage name: "+head; result.stages.clear(); return result; } }
        // canonical lowercase
        for(char& c: head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        specOut.name = head;
        if(!paramBlock.empty()){
            // Split by commas at depth 0 (no nested parens expected here)
            std::vector<std::string> paramsRaw; std::string ptok; int depth=0;
            for(size_t i=0;i<paramBlock.size();++i){ char c=paramBlock[i]; if(c=='('){ ++depth; ptok.push_back(c);} else if(c==')'){ if(depth>0) --depth; ptok.push_back(c);} else if(c==',' && depth==0){ trim(ptok); if(!ptok.empty()) paramsRaw.push_back(ptok); ptok.clear(); } else ptok.push_back(c);} trim(ptok); if(!ptok.empty()) paramsRaw.push_back(ptok);
            for(auto& pr : paramsRaw){ size_t eq = pr.find('='); if(eq==std::string::npos){ result.ok=false; result.error="param missing '=' in stage '"+head+"': "+pr; result.stages.clear(); return result; } std::string k = pr.substr(0,eq); std::string v = pr.substr(eq+1); trim(k); trim(v); if(k.empty()||v.empty()){ result.ok=false; result.error="empty key or value in stage '"+head+"'"; result.stages.clear(); return result; } // lowercase key
                for(char& c: k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                specOut.params.emplace(std::move(k), std::move(v)); }
        }
        result.stages.emplace_back(std::move(specOut));
    }
    return result;
}

} // namespace caldera::backend::processing
