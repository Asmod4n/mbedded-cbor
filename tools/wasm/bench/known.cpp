#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace
{

std::shared_ptr<std::string const> loaded;

cbor::lazy root()
{
    return *cbor::lazy::view(loaded);
}

} // namespace

extern "C" {

__attribute__((export_name("alloc"))) void *alloc(std::size_t const size)
{
    return std::malloc(size);
}

__attribute__((export_name("release"))) void release(void *const ptr)
{
    std::free(ptr);
}

__attribute__((export_name("load"))) std::int32_t load(char const *const ptr, std::size_t const len)
{
    loaded = std::make_shared<std::string const>(ptr, len);
    return cbor::lazy::view(loaded).has_value() ? 0 : -1;
}

__attribute__((export_name("twitter_one"))) std::int32_t twitter_one()
{
    auto const v = root().at_path<"$.statuses[50].user.screen_name", std::string_view>();
    return v ? static_cast<std::int32_t>((*v)->size()) : -1;
}

__attribute__((export_name("twitter_users"))) std::int32_t twitter_users()
{
    auto const v = root().at_path<"$.statuses[*].user.id", cbor::nodelist>();
    return v ? static_cast<std::int32_t>(v->size()) : -1;
}

__attribute__((export_name("twitter_filter"))) std::int32_t twitter_filter()
{
    auto const v = root().at_path<"$.statuses[?@.user.screen_name == 'IwiAlohomora'].id_str", cbor::nodelist>();
    return v ? static_cast<std::int32_t>(v->size()) : -1;
}

__attribute__((export_name("canada_sum"))) double canada_sum()
{
    auto const v = root().at_path<"$.features[0].geometry.coordinates[*][*][0]", cbor::nodelist>();
    double sum = 0;
    if (v)
        for (auto const &n : *v)
            sum += *n.get<double>();
    return sum;
}

__attribute__((export_name("citm_areas"))) std::int32_t citm_areas()
{
    auto const v = root().at_path<"$.performances[*].seatCategories[*].areas[*].areaId", cbor::nodelist>();
    return v ? static_cast<std::int32_t>(v->size()) : -1;
}
}
