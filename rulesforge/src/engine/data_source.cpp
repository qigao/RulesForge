#include "engine/data_source.hpp"

namespace rulesforge {

DataSource::DataSource(::Fact* fact)
    : fact_(fact) {}

DataSource DataSource::fact(::Fact* f) {
    return DataSource(f);
}

::Fact* DataSource::get_fact() const {
    return fact_;
}

} // namespace rulesforge
