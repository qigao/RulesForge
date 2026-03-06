#ifndef QUERY_ENGINE_HPP
#define QUERY_ENGINE_HPP

#include <memory>
#include <string>
#include <vector>

#include "engine/query_result.hpp"

struct Fact;
class KnowledgeBase;
class StatefulSession;

class QueryEngine {
public:
    explicit QueryEngine(std::shared_ptr<KnowledgeBase const> kb);

    QueryResult execute(std::string const& query_name,
                        std::vector<Fact*> const& args,
                        StatefulSession& session) const;

private:
    std::shared_ptr<KnowledgeBase const> kb_;
};

#endif  // QUERY_ENGINE_HPP
