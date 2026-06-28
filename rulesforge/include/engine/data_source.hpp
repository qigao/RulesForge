#ifndef DATA_SOURCE_HPP
#define DATA_SOURCE_HPP

// Forward declaration in global namespace
struct Fact;

namespace rulesforge {

enum class DataSourceType {
    FACT
};

class DataSource {
public:
    static DataSource fact(::Fact* f);

    DataSourceType type() const { return type_; }

    ::Fact* get_fact() const;

private:
    explicit DataSource(::Fact* fact);

    DataSourceType type_ = DataSourceType::FACT;
    ::Fact* fact_ = nullptr;
};

} // namespace rulesforge

#endif // DATA_SOURCE_HPP
