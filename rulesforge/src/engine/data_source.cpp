#include "engine/data_source.hpp"
#include "core/fact.hpp"

namespace rulesforge {

DataSource::DataSource(DataSourceType type,
                       std::variant<::Fact*, std::string, std::vector<uint8_t>> data,
                       std::string path_or_filter)
    : type_(type), data_(std::move(data)), path_or_filter_(std::move(path_or_filter)) {}

DataSource DataSource::fact(::Fact* f) {
    return DataSource(DataSourceType::FACT, f);
}

DataSource DataSource::json(std::string const& content, std::string const& jmespath) {
    return DataSource(DataSourceType::JSON, content, jmespath);
}

DataSource DataSource::csv(std::string const& path, std::string const& filter) {
    return DataSource(DataSourceType::CSV, path, filter);
}

DataSource DataSource::dsv(std::string const& content, std::string const& filter) {
    return DataSource(DataSourceType::DSV, content, filter);
}

DataSource DataSource::binary(std::vector<uint8_t> const& data) {
    return DataSource(DataSourceType::BINARY, data);
}

::Fact* DataSource::get_fact() const {
    return std::get<::Fact*>(data_);
}

std::string const& DataSource::get_content() const {
    return std::get<std::string>(data_);
}

std::string const& DataSource::get_path_or_filter() const {
    return path_or_filter_;
}

std::vector<uint8_t> const& DataSource::get_binary() const {
    return std::get<std::vector<uint8_t>>(data_);
}

} // namespace rulesforge
