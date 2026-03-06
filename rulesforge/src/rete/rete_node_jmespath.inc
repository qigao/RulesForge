// --- SourceExtractNode ---
SourceExtractNode::SourceExtractNode(ParsedJmesPath const& jp_info, std::string fact_type,
                                     std::map<std::string, int> const& bindings,
                                     std::vector<ParsedConstraint> constraints) :
    ReteNode(NodeKind::JmesPath), info(jp_info), result_fact_type(std::move(fact_type)),
    binding_to_token_idx(bindings), constraints_(std::move(constraints)) {}

static std::optional<std::string> read_all_text_file(std::string const& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static std::string strip_dsv_header_suffix(std::string const& header) {
    if (header.size() > 2 && header[header.size() - 2] == '_') {
        char t = header.back();
        if (t == 'n' || t == 's' || t == 'b') {
            return header.substr(0, header.size() - 2);
        }
    }
    return header;
}

static bool parse_int64_str(char const* s, int64_t& out) {
    if (!s || *s == '\0') return false;
    char* end = nullptr;
    long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

static bool parse_double_str(char const* s, double& out) {
    if (!s || *s == '\0') return false;
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s || *end != '\0') return false;
    out = v;
    return true;
}

void SourceExtractNode::left_activate(StatefulSession& session, Token const& token) {
    auto& mem = session.net_mem().jmespath[mem_slot];

    if (token.type == PropagationType::RETRACT) {
        auto it = mem.parent_to_children.find(token.wme);
        if (it != mem.parent_to_children.end()) {
            for (auto const& child_wme : it->second.second) {
                Token child_token{child_wme, PropagationType::RETRACT};
                for (auto* child : children_raw) { child->left_activate(session, child_token); }
            }
            mem.parent_to_children.erase(it);
        }
        return;
    }

    try {
        std::string source_json_text;
        if (info.input_kind == ParsedJmesPath::InputKind::JsonFile) {
            auto file_text = read_all_text_file(info.input_value);
            if (!file_text) {
                logw("SourceExtractNode: failed to read source file '{}'", info.input_value);
                return;
            }
            source_json_text = std::move(*file_text);
        } else {
            source_json_text = info.input_value;
        }
        std::vector<TokenWME const*> child_wmes;
        if (info.engine_kind == ParsedJmesPath::EngineKind::DsvFilter) {
            turbo_csv_doc_t* doc = nullptr;
            int rc = turbo_parse_csv(reinterpret_cast<uint8_t const*>(source_json_text.data()),
                                     source_json_text.size(), &doc);
            if (rc != 0 || !doc) {
                loge("SourceExtractNode(dsv): failed to parse CSV/DSV input");
                return;
            }
            turbo_dsv_filter_t* filter = turbo_dsv_filter_create(doc, 0);
            if (!filter) {
                turbo_free_csv(doc);
                loge("SourceExtractNode(dsv): failed to create DSV filter");
                return;
            }
            if (!turbo_dsv_filter_compile(filter, info.expression.c_str())) {
                char const* err = turbo_dsv_filter_error(filter);
                loge("SourceExtractNode(dsv): filter compile failed: {}", err ? err : "<unknown>");
                turbo_dsv_filter_destroy(filter);
                turbo_free_csv(doc);
                return;
            }

            size_t rows = turbo_csv_row_count(doc);
            size_t cols = turbo_csv_column_count(doc);
            child_wmes.reserve(rows > 0 ? rows - 1 : 0);
            for (size_t row = 1; row < rows; ++row) {
                int matched = turbo_dsv_filter_check_row(filter, row);
                if (matched != 1) continue;

                Fact* new_fact = session.create_fact(result_fact_type);
                for (size_t col = 0; col < cols; ++col) {
                    char const* header = turbo_csv_get(doc, 0, col);
                    if (!header || *header == '\0') continue;
                    std::string field_name = strip_dsv_header_suffix(header);
                    char const* cell = turbo_csv_get(doc, row, col);
                    if (!cell) continue;

                    int64_t int_val = 0;
                    double dbl_val = 0.0;
                    if (parse_int64_str(cell, int_val)) {
                        new_fact->fields[field_name] = int_val;
                    } else if (parse_double_str(cell, dbl_val)) {
                        new_fact->fields[field_name] = dbl_val;
                    } else {
                        new_fact->fields[field_name] = std::string(cell);
                    }
                }

                if (!constraints_.empty()
                    && !check_all_join_conditions(session, token, *new_fact, constraints_, binding_to_token_idx))
                {
                    continue;
                }

                auto new_wme = session.get_or_create_wme(token.wme, new_fact);
                child_wmes.push_back(new_wme);
                Token new_token{new_wme, PropagationType::ASSERT};
                for (auto* c : children_raw) { c->left_activate(session, new_token); }
            }
            turbo_dsv_filter_destroy(filter);
            turbo_free_csv(doc);
        } else {
            jsoncons::json data = jsoncons::json::parse(source_json_text);
            jsoncons::json result = jsoncons::jmespath::search(data, info.expression);

            if (!result.is_array()) {
                logw("SourceExtractNode: expression '{}' did not return array", info.expression);
                return;
            }

            child_wmes.reserve(result.size());
            for (auto const& item : result.array_range()) {
                Fact* new_fact = session.create_fact(result_fact_type);

                if (item.is_object()) {
                    for (auto const& member : item.object_range()) {
                        std::string key = member.key();
                        auto const& val = member.value();
                        if (val.is_string()) { new_fact->fields[key] = val.as<std::string>(); }
                        else if (val.is_bool()) { new_fact->fields[key] = static_cast<int64_t>(val.as<bool>() ? 1 : 0); }
                        else if (val.is_int64()) { new_fact->fields[key] = val.as<int64_t>(); }
                        else if (val.is_uint64()) { new_fact->fields[key] = static_cast<int64_t>(val.as<uint64_t>()); }
                        else if (val.is_double()) { new_fact->fields[key] = val.as<double>(); }
                        else if (val.is_number()) { new_fact->fields[key] = val.as<double>(); }
                    }
                } else if (item.is_int64()) {
                    new_fact->fields["value"] = item.as<int64_t>();
                } else if (item.is_uint64()) {
                    new_fact->fields["value"] = static_cast<int64_t>(item.as<uint64_t>());
                } else if (item.is_double()) {
                    new_fact->fields["value"] = item.as<double>();
                } else if (item.is_number()) {
                    new_fact->fields["value"] = item.as<double>();
                } else if (item.is_string()) {
                    new_fact->fields["value"] = item.as<std::string>();
                }

                if (!constraints_.empty()
                    && !check_all_join_conditions(session, token, *new_fact, constraints_, binding_to_token_idx))
                {
                    continue;
                }

                auto new_wme = session.get_or_create_wme(token.wme, new_fact);
                child_wmes.push_back(new_wme);
                Token new_token{new_wme, PropagationType::ASSERT};
                for (auto* c : children_raw) { c->left_activate(session, new_token); }
            }
        }

        mem.parent_to_children[token.wme] = {token.wme, std::move(child_wmes)};

    } catch (jsoncons::jmespath::jmespath_error const& e) {
        loge("SourceExtractNode: JMESPath error: {}", e.what());
    } catch (std::exception const& e) {
        loge("SourceExtractNode: error: {}", e.what());
    }
}

void SourceExtractNode::print_node(std::ostream& os) const {
    os << "  \"" << id << "\" [label=\"SourceExtractNode (" << id << ")\\nSource: ";
    if (info.engine_kind == ParsedJmesPath::EngineKind::DsvFilter) {
        os << "dsv/";
    } else {
        os << "json/";
    }
    if (info.input_kind == ParsedJmesPath::InputKind::JsonFile) {
        os << "file(" << info.input_value << ")";
    } else {
        os << "string";
    }
    os
       << "\\nExpr: " << info.expression << "\", shape=trapezium, style=filled, fillcolor=lightskyblue];";
}
