#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <utility>
#include <cfloat>

struct Value;

struct Object {
    std::vector<std::pair<std::string, Value>> fields;
    void add(const std::string& key, const Value& v);
    Value toValue() const;
};

struct List {
    std::vector<Value> elements;
    void add(const Value& v);
    Value toValue() const;
};

struct Custom {
    uint8_t id;
    std::vector<uint8_t> data;

    Custom() : id(0) {}
    Custom(uint8_t _id, std::vector<uint8_t>&& _data)
        : id(_id), data(std::move(_data)) {}

    Value toValue() const;
};

using ValueVariant = std::variant<
    std::monostate,
    std::string,
    int64_t,
    double,
    bool,
    uint8_t,
    Object,
    List,
    Custom
>;

enum class ValueType : uint8_t {
    Null,
    String,
    Integer,
    Float,
    Boolean,
    Byte,
    Object,
    List,
    Custom
};

struct Value {
    ValueVariant data;

    Value() : data(std::monostate{}) {}
    Value(int v) : data((int64_t)v) {}
    Value(int64_t v) : data(v) {}
    Value(double v) : data(v) {}
    Value(const std::string& v) : data(v) {}
    Value(std::string&& v) : data(std::move(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(bool v) : data(v) {}
    Value(uint8_t v) : data(v) {}
    Value(const Object& v) : data(v) {}
    Value(Object&& v) : data(std::move(v)) {}
    Value(const List& v) : data(v) {}
    Value(List&& v) : data(std::move(v)) {}
    Value(const Custom& v) : data(v) {}
    Value(Custom&& v) : data(std::move(v)) {}

    static Value Null() { return Value(); }

    ValueType type() const {
        return std::visit([](auto&& arg) -> ValueType {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) return ValueType::String;
            else if constexpr (std::is_same_v<T, int64_t>) return ValueType::Integer;
            else if constexpr (std::is_same_v<T, double>) return ValueType::Float;
            else if constexpr (std::is_same_v<T, bool>) return ValueType::Boolean;
            else if constexpr (std::is_same_v<T, uint8_t>) return ValueType::Byte;
            else if constexpr (std::is_same_v<T, Object>) return ValueType::Object;
            else if constexpr (std::is_same_v<T, List>) return ValueType::List;
            else if constexpr (std::is_same_v<T, Custom>) return ValueType::Custom;
            else return ValueType::Null;
        }, data);
    }
};
