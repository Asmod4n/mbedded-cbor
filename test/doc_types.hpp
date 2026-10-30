#pragma once

#include <cbor/cbor.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace doc_reference_schema
{

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
};

struct point {
    float x;
    float y;
};

} // namespace doc_reference_schema

template <>
struct cbor::describe_members<doc_reference_schema::tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1800};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::tire, diameter),
        CBOR_MEMBER(doc_reference_schema::tire, pressure),
    };
};

template <>
struct cbor::describe_members<doc_reference_schema::car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1801};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::car, make),
        CBOR_MEMBER(doc_reference_schema::car, seats),
        CBOR_MEMBER(doc_reference_schema::car, tires),
        CBOR_MEMBER(doc_reference_schema::car, track),
    };
};

template <>
struct cbor::describe_members<doc_reference_schema::point> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1802};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::point, x),
        CBOR_MEMBER(doc_reference_schema::point, y),
    };
};

namespace reference_structs
{

struct person {
    std::string name;
    int age;
};

struct header {
    std::string content_type;
};

struct senml {
    std::string n;
    double v;
};

struct account {
    std::string user;
    std::uint8_t level;
};

struct reading {
    std::uint8_t id;
    std::optional<std::uint8_t> unit;
};

struct trailer {
    std::uint8_t checksum;
};

} // namespace reference_structs

template <>
struct cbor::describe_members<reference_structs::header> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160060};
    static constexpr std::tuple members{
        cbor::member{"content-type", &reference_structs::header::content_type},
    };
};

template <>
struct cbor::describe_members<reference_structs::senml> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &reference_structs::senml::n},
        cbor::member{2, &reference_structs::senml::v},
    };
};

template <>
struct cbor::describe_members<reference_structs::account> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        CBOR_MEMBER(reference_structs::account, user),
        CBOR_MEMBER(reference_structs::account, level),
    };
};

CBOR_DEFINE_TYPE(reference_structs::trailer, cbor::tag{160061},
                 cbor::member{"checksum", &reference_structs::trailer::checksum});
