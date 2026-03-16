#ifndef DATA_SOURCE_HPP
#define DATA_SOURCE_HPP

#include <string>
#include <vector>
#include <variant>
#include <memory>

// Forward declaration in global namespace
struct Fact;

namespace rulesforge {

enum class DataSourceType {
    FACT,
    JSON,
    CSV,
    DSV,
    BINARY
};

class DataSource {
public:
    static DataSource fact(::Fact* f);
    static DataSource json(std::string const& content, std::string const& jmespath = "");
    static DataSource csv(std::string const& path, std::string const& filter = "");
    static DataSource dsv(std::string const& content, std::string const& filter = "");
    static DataSource binary(std::vector<uint8_t> const& data);

    DataSourceType type() const { return type_; }

    ::Fact* get_fact() const;
    std::string const& get_content() const;
    std::string const& get_path_or_filter() const;
    std::vector<uint8_t> const& get_binary() const;

private:
    DataSource(DataSourceType type,
               std::variant<::Fact*, std::string, std::vector<uint8_t>> data,
               std::string path_or_filter = "");

    DataSourceType type_;
    std::variant<::Fact*, std::string, std::vector<uint8_t>> data_;
    std::string path_or_filter_;
};

} // namespace rulesforge

#endif // DATA_SOURCE_HPP
